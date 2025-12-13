#include <bits/stdc++.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>
#include <numeric>
#include <omp.h>
using json = nlohmann::json;
using namespace std;
using namespace std::chrono;

struct Graph {
    int n;
    vector<vector<int>> adj;
    vector<string> names;
    unordered_map<string, int> nameToId;
    double d_avg;

    Graph(int n=0): n(n), adj(n) {}

    void addVertex(const string& name) {
        int id = names.size();
        names.push_back(name);
        nameToId[name] = id;
        if ((int)adj.size() < id+1) adj.resize(id+1);
        n = id+1;
    }

    void addEdge(const string& u, const string& v) {
        int uid = nameToId[u], vid = nameToId[v];
        adj[uid].push_back(vid);
        adj[vid].push_back(uid);
    }

    void removeVertex(int id) {
        for (int v : adj[id]) {
            adj[v].erase(remove(adj[v].begin(), adj[v].end(), id), adj[v].end());
        }
        adj[id].clear();
    }

    bool valid(int id) const {
        return !adj[id].empty() || !names[id].empty();
    }

    void D_avg() {
        d_avg = 0;
        for (int i=0; i<n; i++) {
            d_avg += adj[i].size();
        }
        d_avg /= n;
    }
};

Graph loadTopologyFromJson(const string& filepath) {
    Graph g;
    ifstream f(filepath);
    json data = json::parse(f);

    // add vertices
    for (const auto& router : data["routers"]) {
        string name = to_string(router["idx"].get<int>());
        g.addVertex(name);
    }

    // add edges
    for (const auto& router : data["routers"]) {
        string u = to_string(router["idx"].get<int>());
        for (const auto& neighbor : router["neighbors"]) {
            string v = to_string(neighbor["peeridx"].get<int>());
            g.addEdge(u, v);
        }
    }
    return g;
}

Graph createFatTree(int k) {
    if (k % 2 != 0 || k <= 0) throw runtime_error("k must be a positive even number");
    Graph g;

    // core
    for (int i=0; i<k/2; i++) {
        for (int j=0; j<k/2; j++) {
            g.addVertex("c_" + to_string(i) + "_" + to_string(j));
        }
    }

    // pods
    for (int p=0; p<k; p++) {
        for (int s=0; s<k/2; s++) g.addVertex("a_" + to_string(p) + "_" + to_string(s));
        for (int s=0; s<k/2; s++) g.addVertex("e_" + to_string(p) + "_" + to_string(s));
    }

    // agg <-> edge links
    for (int p=0; p<k; p++) {
        for (int sAgg=0; sAgg<k/2; sAgg++) {
            string agg = "a_" + to_string(p) + "_" + to_string(sAgg);
            for (int sEdge=0; sEdge<k/2; sEdge++) {
                string edge = "e_" + to_string(p) + "_" + to_string(sEdge);
                g.addEdge(agg, edge);
            }
        }
    }

    // core <-> agg links
    for (int j=0; j<k/2; j++) {
        for (int m=0; m<k/2; m++) {
            string core = "c_" + to_string(j) + "_" + to_string(m);
            for (int p=0; p<k; p++) {
                string agg = "a_" + to_string(p) + "_" + to_string(j);
                g.addEdge(core, agg);
            }
        }
    }
    return g;
}

vector<int> bfs(const Graph& g, int src, int cutoff) {
    vector<int> dist(g.n, -1);
    queue<int> q;
    dist[src] = 0;
    q.push(src);
    while(!q.empty()) {
        int u = q.front(); q.pop();
        if (cutoff >= 0 && dist[u] >= cutoff) continue;
        for (int v : g.adj[u]) {
            if (dist[v] == -1) {
                dist[v] = dist[u] + 1;
                q.push(v);
            }
        }
    }
    return dist;
}

vector<double> betweenness(const Graph& g, bool useApprox, int cutoff) {
    // TODO: Needs optimization. Complexity is O(n^2 + n*m). Consider sampling for approximation.
    int n = g.n;
    vector<double> CB(n, 0.0);
    for (int s = 0; s < n; s++) {
        auto dist = bfs(g, s, useApprox ? cutoff : -1);
        for (int t = 0; t < n; t++) {
            if (t==s || dist[t] <= 0) continue;
            CB[t] += 1.0; // simplified: count visit times only, not full Brandes algorithm
        }
    }
    return CB;
}

vector<double> betweenness_approx(const Graph& g, bool useApprox, int cutoff) {
    int n = g.n;
    vector<double> CB(n, 0.0);

    // determine k (number of samples)
    int k;
    if(n<1000) k=n;
    else{
        k=100*log(n);
    }
    
    // randomly select k sources (deterministic seed)
    vector<int> sources(k);
    mt19937 rng(1);

    uniform_int_distribution<int> distribute(0, n - 1);
    
    for (int i = 0; i < k; i++) {
        sources[i] = distribute(rng);
    }

    // per-sample data structures
    vector<int> sigma(n), dist(n);
    vector<vector<int>> pred(n);
    vector<double> delta(n);
    vector<double> local_CB(n, 0.0); // thread-local accumulator
        
    for (int i = 0; i < k; i++) {
        int s = sources[i];
        
        // initialize state for current source
        fill(sigma.begin(), sigma.end(), 0);
        fill(dist.begin(), dist.end(), -1);
        fill(delta.begin(), delta.end(), 0.0);
        for (auto& p : pred) p.clear();
        
        sigma[s] = 1;
        dist[s] = 0;
        
        queue<int> q;
        q.push(s);
        stack<int> st;
        
        // BFS phase: compute shortest paths and predecessors
        while (!q.empty()) {
            int u = q.front(); q.pop();
            st.push(u);
            
            // early termination condition
            if (useApprox && dist[u] > cutoff) {
                continue;
            }
            
            for (int v : g.adj[u]) {
                if (dist[v] < 0) { // first visit
                    dist[v] = dist[u] + 1;
                    q.push(v);
                }
                if (dist[v] == dist[u] + 1) { // shortest path
                    sigma[v] += sigma[u];
                    pred[v].push_back(u);
                }
            }
        }
        
        // accumulation phase (back-propagation)
        while (!st.empty()) {
            int w = st.top(); st.pop();
            if (sigma[w] > 0) { // avoid division by zero
                for (int v : pred[w]) {
                    delta[v] += (sigma[v] * 1.0 / sigma[w]) * (1.0 + delta[w]);
                }
            }
            if (w != s) {
                local_CB[w] += delta[w];
            }
        }
    }
    
    // merge local results into global result
    for (int i = 0; i < n; i++) {
        CB[i] += local_CB[i];
    }
    
    // scale to account for sampling so results are comparable to full computation
    double scale = n * 1.0 / k;
    for (int i = 0; i < n; i++) {
        CB[i] *= scale;
    }
    
    return CB;
}

vector<double> betweenness_approx_parallel(const Graph& g, bool useApprox, int cutoff) {
    int n = g.n;
    vector<double> CB(n, 0.0);

    // determine k (number of samples)
    int k;
    if(n<1000) k=n;
    else{
        k=100*log(n);
    }

    // determine thread count
    unsigned int hardware_threads = std::thread::hardware_concurrency();
    omp_set_num_threads(hardware_threads);
    
    // printf("k: %d, hardware_threads: %d\n", k, hardware_threads);
    
    // randomly select k sources (reproducible)
    vector<int> sources(k);
    #pragma omp parallel
    {
        // each thread has its own RNG to avoid contention
        mt19937 rng(omp_get_thread_num() + 42); // seed includes thread ID
        uniform_int_distribution<int> dist(0, n - 1);
        
        #pragma omp for
        for (int i = 0; i < k; i++) {
            sources[i] = dist(rng);
        }
    }
    
    // parallel processing of sampled sources
    #pragma omp parallel
    {
        // thread-local data structures
        vector<int> sigma(n), dist(n);
        vector<vector<int>> pred(n);
        vector<double> delta(n);
        vector<double> local_CB(n, 0.0); // thread-local accumulator
        
        #pragma omp for schedule(dynamic, 1) nowait
        for (int i = 0; i < k; i++) {
            int s = sources[i];
            
            // initialize state for current source
            fill(sigma.begin(), sigma.end(), 0);
            fill(dist.begin(), dist.end(), -1);
            fill(delta.begin(), delta.end(), 0.0);
            for (auto& p : pred) p.clear();
            
            sigma[s] = 1;
            dist[s] = 0;
            
            queue<int> q;
            q.push(s);
            stack<int> st;
            
            // BFS phase: compute shortest paths and predecessors
            while (!q.empty()) {
                int u = q.front(); q.pop();
                st.push(u);
                
                // early termination condition
                if (useApprox && dist[u] > cutoff) {
                    continue;
                }
                
                for (int v : g.adj[u]) {
                    if (dist[v] < 0) { // first visit
                        dist[v] = dist[u] + 1;
                        q.push(v);
                    }
                    if (dist[v] == dist[u] + 1) { // shortest path
                        sigma[v] += sigma[u];
                        pred[v].push_back(u);
                    }
                }
            }
            
            // accumulation phase (back-propagation)
            while (!st.empty()) {
                int w = st.top(); st.pop();
                if (sigma[w] > 0) { // avoid division by zero
                    for (int v : pred[w]) {
                        delta[v] += (sigma[v] * 1.0 / sigma[w]) * (1.0 + delta[w]);
                    }
                }
                if (w != s) {
                    local_CB[w] += delta[w];
                }
            }
        }
        
        // merge thread-local results into the global result
        #pragma omp critical
        for (int i = 0; i < n; i++) {
            CB[i] += local_CB[i];
        }
        // auto t2 = high_resolution_clock::now();
        // printf("execution time: %ld\n", duration_cast<milliseconds>(t2-t1).count());
    }
    
    // scale to account for sampling so results are comparable to full computation
    double scale = n * 1.0 / k;
    for (int i = 0; i < n; i++) {
        CB[i] *= scale;
    }
    
    return CB;
}

// -------------------------------
// Connected components
// -------------------------------
vector<vector<int>> components(const Graph& g,const set<int>& currentCut) {
    // currentCut is passed because cut vertices should not be counted as part of components
    vector<int> vis(g.n, 0);
    vector<vector<int>> comps;
    for (int i=0; i<g.n; i++) {
        // if (!vis[i] && !g.adj[i].empty()) {
        if (!vis[i]) { // if we skip empty nodes, components consisting of a single node may be omitted
            if(currentCut.count(i)) continue;
            vector<int> comp;
            stack<int> st;
            st.push(i);
            vis[i]=1;
            while(!st.empty()) {
                int u = st.top(); st.pop();
                comp.push_back(u);
                for (int v: g.adj[u]) if(!vis[v]) {
                    vis[v]=1; st.push(v);
                }
            }
            comps.push_back(comp);
        }
    }
    return comps;
}

// -------------------------------
// Evaluate cut set score
// -------------------------------
double evaluate(const Graph& original, const set<int>& cutSet, const vector<vector<int>>& comps,
                double w1, double w2, double w3, double w4) {
    // Using three metrics:
    // 1) The maximum of (component sizes and cut set size) relative to total nodes (range 0..1), smaller is better
    // 2) Density of edges between cut set and components, normalized by average degree to remove scale, smaller is better
    // 3) Balance of component sizes (same as before)
    // w1 is unused
    int k = comps.size();
    if (k == 0) return 1e18;

    // term2: max ratio of cutset size or any component size to total nodes
    double term2=(double)cutSet.size()/original.n;
    for (auto& comp : comps) {
        term2=max(term2, (double)comp.size()/original.n);
    }

    // term3: average connectivity from cutSet to each component, normalized by average degree
    double sumConn = 0.0;
    // iterate over cutset and count edges to each node
    unordered_map<int,int> nodeLink(original.n);
    for (int u : cutSet) {
        for (int v : original.adj[u]) {
            nodeLink[v]++;
        }
    }
    for (const auto& comp : comps) {
        int edges = 0;
        for (int u : comp) {
            edges += nodeLink[u];
        }
        if (!comp.empty()) sumConn += (double)edges / comp.size();
    }

    double term3 = sumConn / k / original.d_avg;

    // term4: component size balance (coefficient of variation)
    vector<int> sizes;
    for (auto& comp : comps) sizes.push_back(comp.size());
    double mean = accumulate(sizes.begin(), sizes.end(), 0.0) / sizes.size();
    double var = 0;
    for (int s : sizes) var += (s-mean)*(s-mean);
    var /= sizes.size();
    double stddev = sqrt(var);
    double term4 = mean > 0 ? stddev / mean : 0;

    return w2*term2 + w3*term3 + w4*term4;
}

// -------------------------------
// Core algorithm
// -------------------------------
tuple<set<int>, vector<vector<int>>, double>
findCutSet(Graph g, const Graph& original, bool useApprox, int cutoff,
           double w1, double w2, double w3, double w4) {
    int n = g.n;
    set<int> bestCut;
    vector<vector<int>> bestComps;
    double bestScore = 1e18;

    set<int> currentCut;

    for (int iter=0; iter<n; iter++) {
        auto centrality = betweenness_approx_parallel(g, useApprox, cutoff);
        if (centrality.empty()) break;
        int v = max_element(centrality.begin(), centrality.end()) - centrality.begin();
        if (g.adj[v].empty()) break;

        currentCut.insert(v);
        g.removeVertex(v);

        auto comps = components(g,currentCut);
        if (comps.size() > 1) {
            double score = evaluate(original, currentCut, comps, w1,w2,w3,w4);
            if (score < bestScore) {
                bestScore = score;
                bestCut = currentCut;
                bestComps = comps;
            }
        }
    }
    return {bestCut, bestComps, bestScore};
}

// Greedy assignment helper
pair<bool, vector<vector<int>>> tryGreedyAssignment(const vector<pair<int, int>>& indexedSizes, int k, int maxSize) {
    vector<vector<int>> groups(k);
    vector<int> groupSums(k, 0);
    
    for (const auto& [size, originalIdx] : indexedSizes) {
        bool placed = false;
        
        // try to place current item into the first group that fits
        for (int i = 0; i < k; i++) {
            if (groupSums[i] + size <= maxSize) {
                groups[i].push_back(originalIdx);
                groupSums[i] += size;
                placed = true;
                break;
            }
        }
        
        if (!placed) {
            return {false, {}};
        }
    }
    
    return {true, groups};
}

// Best-fit greedy strategy
pair<bool, vector<vector<int>>> tryGreedyBestFit(const vector<pair<int, int>>& indexedSizes, int k, int maxSize) {
    vector<vector<int>> groups(k);
    vector<int> groupSums(k, 0);
    
    for (const auto& [size, originalIdx] : indexedSizes) {
        int bestIdx = -1;
        int minRemaining = INT_MAX;
        
        // find best bin (smallest remaining space after placing)
        for (int i = 0; i < k; i++) {
            if (groupSums[i] + size <= maxSize) {
                int remaining = maxSize - (groupSums[i] + size);
                if (remaining < minRemaining) {
                    minRemaining = remaining;
                    bestIdx = i;
                }
            }
        }
        
        if (bestIdx == -1) {
            return {false, {}};
        }
        
        groups[bestIdx].push_back(originalIdx);
        groupSums[bestIdx] += size;
    }
    
    return {true, groups};
}

// Main function: greedy + binary search to find minimal maximal group size
pair<int, vector<vector<int>>> greedyPartitionWithIndices(const vector<vector<int>>& comps, int k, bool useBestFit = false) {
    vector<int> sizes;
    for (auto& comp : comps) sizes.push_back(comp.size());
    int n = sizes.size();
    if (n == 0 || k <= 0) {
        return {0, {}};
    }
    
    // preserve original indices
    vector<pair<int, int>> indexedSizes;
    for (int i = 0; i < n; i++) {
        indexedSizes.emplace_back(sizes[i], i);
    }
    
    // sort by size descending
    sort(indexedSizes.begin(), indexedSizes.end(), 
         [](const pair<int, int>& a, const pair<int, int>& b) {
             return a.first > b.first;
         });
    
    // binary search bounds
    int low = *max_element(sizes.begin(), sizes.end());
    int high = accumulate(sizes.begin(), sizes.end(), 0);
    
    // special cases
    if (k == 1) {
        std::vector<int> vec(n);
        std::iota(vec.begin(),vec.end(),0);
        return {high, {vec}};
    }
    if (k >= n) {
        vector<vector<int>> result;
        for (int i = 0; i < n; i++) {
            result.push_back({i});
        }
        return {low, result};
    }
    
    int minMaxSize = high;
    vector<vector<int>> bestAssignment;
    
    // binary search
    while (low <= high) {
        int mid = low + (high - low) / 2;
        pair<bool, vector<vector<int>>> result;
        
        if (useBestFit) {
            result = tryGreedyBestFit(indexedSizes, k, mid);
        } else {
            result = tryGreedyAssignment(indexedSizes, k, mid);
        }
        
        if (result.first) {
            // successful assignment, try smaller max_size
            minMaxSize = mid;
            bestAssignment = result.second;
            high = mid - 1;
        } else {
            // failed, need larger max_size
            low = mid + 1;
        }
    }
    
    return {minMaxSize, bestAssignment};
}

// Print partition result
void printPartitionResult(const Graph& g, const vector<vector<int>>& comps, int minMaxSize, 
                         const vector<vector<int>>& assignment, const string& strategyName) {
    cout << "\n=== " << strategyName << " ===" << endl;
    cout << "Min max group size: " << minMaxSize << endl;
    cout << "Partition result:" << endl;
    
    for (size_t i = 0; i < assignment.size(); i++) {
        int groupSum = 0;
        
        for (int idx : assignment[i]) {
            groupSum += comps[idx].size();
        }
        
        cout << "Group " << i + 1 << ": [";
        for (size_t j = 0; j < assignment[i].size(); j++) {
            for(auto &comp : comps[assignment[i][j]]) cout << g.names[comp] << " ";
            if (j < assignment[i].size() - 1) cout << ", ";
        }
        cout << "]";
        cout << " Sum=" << groupSum << endl;
    }
}

int main(int argc,char* argv[]) {
    if(argc!=3){
        cerr<<"usage: " << argv[0] << " <group_num> <path>"<<endl;
        cerr<<"<group_num>: number of groups"<<endl;
        exit(1);
    }

    int group_num=stoi(argv[1]);
    string path = argv[2];
    Graph g = loadTopologyFromJson(path + "/blueprint.json");

    cout << "Total nodes: " << g.n << "\n";
    g.D_avg();
    double w1=0.0, w2=27.0, w3=1.0, w4=1.0;

    int cutoff = 4;

    cout << "\nRunning approximate version (cutoff=" << cutoff << ")..." << endl;
    auto t1 = high_resolution_clock::now();
    auto [cutApprox, compsApprox, scoreApprox] = findCutSet(g, g, true, cutoff, w1,w2,w3,w4);
    auto t2 = high_resolution_clock::now();
    double timeApprox = duration<double>(t2-t1).count();
    cout << "Approximate algorithm time: " << timeApprox << " seconds\n";

    cout << "\n========== Final result ==========\n";
    if (!cutApprox.empty()) {
        cout << "Best score (approx): " << scoreApprox << "\n";
        cout << "Cut set size: " << cutApprox.size() << "\n";
        cout << "Cut set nodes: ";
        for (int v : cutApprox) cout << g.names[v] << " ";
        cout << "\nNumber of components: " << compsApprox.size() << "\n";
        for (int i=0; i<(int)compsApprox.size(); i++) {
            cout << "  Component " << i+1 << " size=" << compsApprox[i].size() << ":";
            for (int v : compsApprox[i]) cout << " " << g.names[v];
            cout << "\n";
        }
    } else {
        cout << "No valid cut set found\n";
    }

    vector<int> cut;
    for (auto u : cutApprox) {
        cut.push_back(u + 1);
    }
    vector<vector<int>> parts_split = compsApprox;
    for (auto &part : parts_split) {
        for (auto &u : part) {
            u++;
        }
    }
    parts_split.push_back(cut);
    json j1 = parts_split;
    ofstream part_split_plan(path + "/partition_split.json");
    part_split_plan << j1 << endl;

    // find concrete partitioning of components into multiple parts
    auto [minMaxSize, assignment] = greedyPartitionWithIndices(compsApprox, group_num, true);
    printPartitionResult(g, compsApprox, minMaxSize, assignment, "Greedy + Binary Search");

    vector<vector<int>> parts;
    for (size_t i = 0; i < assignment.size(); i++) {
        vector<int> group;
        for (size_t j = 0; j < assignment[i].size(); j++) {
            for (auto &comp : compsApprox[assignment[i][j]]) {
                group.push_back(comp + 1);
            }
        }
        parts.push_back(group);
    }
    parts.push_back(cut);
    json j2 = parts;
    ofstream part_plan(path + "/partition.json");
    part_plan << j2 << endl;

    return 0;
}
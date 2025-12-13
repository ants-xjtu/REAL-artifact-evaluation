#pragma once

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <future>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include "json.hpp"

using json = nlohmann::json;

extern int nthreads;

void start_nodes(const std::string& image,
                   const std::unordered_set<int>& nodes,
                   const std::unordered_map<int, std::string>& neighborList,
                   int nNodes,
                   const std::string& logPath);

void restart_nodes(const std::string& image,
                   const std::unordered_set<int>& nodes,
                   const std::unordered_map<int, std::string>& neighborList,
                   int nNodes,
                   const std::string& logPath);

void stop_nodes(const std::string& image, const std::unordered_set<int>& nodes, const std::string& logPath);

void export_routes(const std::string& image, const std::unordered_set<int>& nodes, const std::string& tag, const std::string& logPath);

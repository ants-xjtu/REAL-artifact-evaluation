#pragma once

#include <cstdint>

constexpr const char* MNG_SOCKET_PATH = "/opt/lwc/volumes/ripc/msg_manager_socket";
constexpr long MAX_THREADS = 64;
constexpr long MAX_HOSTS = 64;
constexpr long MAX_CLIENTS = 20000;
constexpr long MAX_CONNS = 1 << 20;
constexpr long PORT_START = 10000;

constexpr long SEC_PER_NS = 1'000'000'000;
constexpr long MSEC_PER_NS = 1'000'000;
constexpr long USEC_PER_NS = 1'000;
constexpr long BUILDUP_TRY_INTERVAL = 1'000'000'000;
constexpr long CONVERGE_TIMEOUT = 3'500'000'000;
constexpr long EXEC_TIMEOUT_IN_SEC = 180;
constexpr long KEEPBUSY_INTERVAL = 100'000'000;

#define BGP_TYPE(buf) (*((u_char *)(buf) + 18))
constexpr long BGP_OPEN = 1;
constexpr long BGP_UPDATE = 2;
constexpr long BGP_NOTIFICATION = 3;
constexpr long BGP_KEEPALIVE = 4;

enum RealMsgType {
    REAL_SYN = 1,
    REAL_SYNACK, // body: 4 byte, 0 stands for ok, other value indicates errno
    REAL_PAYLOAD,
    REAL_ACK,
    REAL_ENDOFSTAGE,
    REAL_KEEPBUSY,
    REAL_MAX_MSGTYPE,
};

enum ConvergeStage {
    STAGE_BUILDUP = 1,
    STAGE_RESTORE,
    STAGE_CONVERGE,
    STAGE_TEARDOWN,
    STAGE_END,
    STAGE_MAX,
};

extern const char *msg_type_name[REAL_MAX_MSGTYPE];
extern const char *stage_name[STAGE_MAX];

typedef struct {
    int32_t msg_type;
    int32_t msg_len;
    int64_t seq;
} real_hdr_t;

typedef struct {
    real_hdr_t hdr;
    int32_t cli_id;
    int32_t svr_id;
    // ignored by controller, only used by
    // listener's shim
    uint16_t cli_port;
} real_syn_t;

typedef struct {
    real_hdr_t hdr;
    uint16_t cli_port;
} real_synack_t;

typedef struct {
    real_hdr_t hdr;
    int32_t src_id;
    int32_t dst_id;
} real_pld_t;

constexpr int hdrsiz = sizeof(real_hdr_t);
constexpr int synsiz = sizeof(real_syn_t);
constexpr int synacksiz = sizeof(real_synack_t);

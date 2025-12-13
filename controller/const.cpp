#include "const.hpp"

const char *msg_type_name[REAL_MAX_MSGTYPE] = {
    "REAL_INVALID",
    "REAL_SYN",
    "REAL_SYNACK",
    "REAL_PAYLOAD",
    "REAL_ACK",
    "REAL_ENDOFSTAGE",
    "REAL_KEEPBUSY",
};

const char *stage_name[STAGE_MAX] = {
    "STAGE_INVALID",
    "STAGE_BUILDUP",
    "STAGE_RESTORE",
    "STAGE_CONVERGE",
    "STAGE_TEARDOWN",
    "STAGE_END",
};

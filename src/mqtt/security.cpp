#include "security.h"
#include <mbedtls/md.h>

#ifndef RATE_LIMIT_MAX
#define RATE_LIMIT_MAX 10
#endif
#ifndef RATE_LIMIT_WINDOW
#define RATE_LIMIT_WINDOW 60000
#endif
#ifndef RATE_LIMIT_BLOCK
#define RATE_LIMIT_BLOCK 300000
#endif
#ifndef RATE_LIMIT_ATTEMPTS
#define RATE_LIMIT_ATTEMPTS 10
#endif

struct RateLimitEntry {
    unsigned long windowStart;
    int failCount;
    unsigned long blockedUntil;
};

extern RateLimitEntry rateLimitEntries[RATE_LIMIT_MAX];
extern int rateLimitIndex;

String calcularHMAC(String payload, String secret) {
    byte hmacResult[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char*)secret.c_str(), secret.length());
    mbedtls_md_hmac_update(&ctx, (const unsigned char*)payload.c_str(), payload.length());
    mbedtls_md_hmac_finish(&ctx, hmacResult);
    mbedtls_md_free(&ctx);

    String tokenHex = "";
    for (int i = 0; i < 32; i++) {
        char buf[3];
        sprintf(buf, "%02x", hmacResult[i]);
        tokenHex += buf;
    }
    return tokenHex;
}

bool checkRateLimit(String clientId) {
    unsigned long now = millis();
    for (int i = 0; i < RATE_LIMIT_MAX; i++) {
        if (rateLimitEntries[i].windowStart == 0) continue;
        if (clientId.length() > 0) {
            if (now < rateLimitEntries[i].blockedUntil) return false;
        }
    }
    for (int i = 0; i < RATE_LIMIT_MAX; i++) {
        if (rateLimitEntries[i].windowStart == 0 ||
            (now - rateLimitEntries[i].windowStart) > RATE_LIMIT_WINDOW) {
            rateLimitEntries[i].windowStart = now;
            rateLimitEntries[i].failCount++;
            if (rateLimitEntries[i].failCount >= RATE_LIMIT_MAX) {
                rateLimitEntries[i].blockedUntil = now + RATE_LIMIT_BLOCK;
                rateLimitEntries[i].failCount = 0;
                return false;
            }
            return true;
        }
    }
    return true;
}

void resetRateLimit() {
    for (int i = 0; i < RATE_LIMIT_MAX; i++) {
        rateLimitEntries[i].failCount = 0;
        rateLimitEntries[i].blockedUntil = 0;
    }
}

int roleRank(String role) {
    if (role == "master") return 3;
    if (role == "installer") return 2;
    return 1; // admin
}

bool hasRolePermission(String requiredRole, String userRole) {
    return roleRank(userRole) >= roleRank(requiredRole);
}

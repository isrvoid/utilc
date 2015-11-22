#include <stdint.h>

#include "compositeTypes.h"

typedef struct {
    int foo; // FIXME
} notification_t;

typedef void (*ntfy_delegate_t)(uint32_t notificationId, constFatPtr_t msg, uint32_t senderId);

int ntfy_init(void);
int ntfy_post(uint32_t notificationId);
//int ntfy_postWith

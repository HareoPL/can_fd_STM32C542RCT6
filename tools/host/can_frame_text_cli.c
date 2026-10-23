/**
 * @file can_frame_text_cli.c
 * @brief Host harness for app/can_bus/can_uart_bridge/can_frame_text.c (tools/test_can_frame_text_host.py).
 *
 * stdin:  one vector per line: "<mode>\t<text>" (mode: auto, classic, fd_brs, fd_no_brs, tlv)
 * stdout: "OK\t<bus>\t<ext>\t<fd>\t<brs>\t<rtr>\t<len>\t<hex data>\t<display>" or "ERR\t<message>"
 */

#include "can_frame_text.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    char line[1024];

    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        line[strcspn(line, "\n")] = '\0';

        char *tab = strchr(line, '\t');
        if (tab == NULL)
        {
            printf("ERR\tharness: missing tab\n");
            continue;
        }
        *tab = '\0';

        cft_mode_t mode;
        if (!can_frame_text_mode_parse(line, &mode))
        {
            printf("ERR\tharness: unknown mode '%s'\n", line);
            continue;
        }

        cft_frame_t frame;
        char err[512];
        if (!can_frame_text_parse(tab + 1, mode, &frame, err, sizeof(err)))
        {
            printf("ERR\t%s\n", err);
            continue;
        }

        const CAN_msg_t *msg = &frame.msg;
        char display[CFT_FORMAT_BUF_SIZE];
        (void)can_frame_text_format(msg, display, sizeof(display));

        printf("OK\t%u\t%d\t%d\t%d\t%d\t%u\t", (unsigned)frame.bus,
               CAN_msg_is_extended_id(msg->ident) ? 1 : 0,
               (msg->format == CAN_MSG_FRAME_FORMAT_FD_CAN) ? 1 : 0,
               ((msg->flags & CAN_MSG_FLAG_BRS) != 0U) ? 1 : 0,
               ((msg->flags & CAN_MSG_FLAG_RTR) != 0U) ? 1 : 0,
               (unsigned)msg->len);
        if ((msg->flags & CAN_MSG_FLAG_RTR) == 0U)
        {
            for (unsigned i = 0U; i < msg->len; i++)
            {
                printf("%02X", msg->data[i]);
            }
        }
        printf("\t%s\n", display);
    }
    return 0;
}

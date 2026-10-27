/**
 * @file io_cmd_cli.c
 * @brief Host harness for app/io/io_cmd.c + app/io/io_sim.c (tools/test_io_cmd_host.py).
 *
 * stdin:  one line per step:
 *           "PHYS <btn_hex> <fuel>"           sets the physical inputs returned by the io_manager stub
 *           "LIMITS <max> <step> <engine_on>"  io_sim_setRpmLimits() (RPM limits of a car simulator)
 *           "VSIG <0|1>"                       io_sim_setVehicleSignalsUsed()
 *           "ODO+ <metres>"                    io_sim_advanceOdometer()
 *           anything else                      passed to io_cmd_process()
 * stdout: one line per step: "<responses joined with ' | '>\t<data0> <data1> <data2>"
 *         where the responses are "NOT_IO <line>" if io_cmd_process() returned false,
 *         and data0..2 are the telemetry frame bytes built from the effective inputs.
 */

#include "io_cmd.h"
#include "io_manager.h"
#include "io_sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t s_phys_btn = 0U;
static uint8_t s_phys_fuel = 0U;
static char s_resp[4096];

/* io_manager stub */
void io_manager_getPhysInputs(uint16_t *btn_state, uint8_t *fuel)
{
    *btn_state = s_phys_btn;
    *fuel = s_phys_fuel;
}

static void out(const char *text)
{
    if (s_resp[0] != '\0')
    {
        strncat(s_resp, " | ", sizeof(s_resp) - strlen(s_resp) - 1U);
    }
    strncat(s_resp, text, sizeof(s_resp) - strlen(s_resp) - 1U);
}

int main(void)
{
    char line[512];

    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        line[strcspn(line, "\r\n")] = '\0';
        s_resp[0] = '\0';

        if (strncmp(line, "PHYS ", 5) == 0)
        {
            char *end;
            s_phys_btn = (uint16_t)strtoul(line + 5, &end, 16);
            s_phys_fuel = (uint8_t)strtoul(end, NULL, 10);
            out("PHYS");
        }
        else if (strncmp(line, "LIMITS ", 7) == 0)
        {
            char *end;
            const uint16_t max = (uint16_t)strtoul(line + 7, &end, 10);
            const uint16_t step = (uint16_t)strtoul(end, &end, 10);
            io_sim_setRpmLimits(max, step, (uint16_t)strtoul(end, NULL, 10));
            out("LIMITS");
        }
        else if (strncmp(line, "VSIG ", 5) == 0)
        {
            io_sim_setVehicleSignalsUsed(strtoul(line + 5, NULL, 10) != 0U);
            out("VSIG");
        }
        else if (strncmp(line, "ODO+ ", 5) == 0)
        {
            io_sim_advanceOdometer((uint32_t)strtoul(line + 5, NULL, 10));
            out("ODO+");
        }
        else
        {
            char copy[512];
            strcpy(copy, line);
            if (!io_cmd_process(line, out))
            {
                /* The line must be left intact for the CAN frame parser */
                out(strcmp(copy, line) == 0 ? "NOT_IO" : "NOT_IO MODIFIED");
                strncat(s_resp, " ", sizeof(s_resp) - strlen(s_resp) - 1U);
                strncat(s_resp, line, sizeof(s_resp) - strlen(s_resp) - 1U);
            }
        }

        io_sim_values_t v;
        io_sim_resolve(s_phys_btn, s_phys_fuel, &v);
        printf("%s\t%02X %02X %02X\n", s_resp, (unsigned)(v.btn & 0xFFU), (unsigned)v.fuel,
               (unsigned)io_sim_rpmToByte(v.rpm));
    }
    return 0;
}

/*****************************************************************************
 * File name: cli_task.c
 *
 * Description: This file implements a terminal UI to configure parameters
 ** for radar presence application.

 *
 * ===========================================================================
 * Copyright (C) 2022 Infineon Technologies AG. All rights reserved.
 * ===========================================================================
 *
 * ===========================================================================
 * Infineon Technologies AG (INFINEON) is supplying this file for use
 * exclusively with Infineon's sensor products. This file can be freely
 * distributed within development tools and software supporting such
 * products.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS".  NO WARRANTIES, WHETHER EXPRESS, IMPLIED
 * OR STATUTORY, INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE.
 * INFINEON SHALL NOT, IN ANY CIRCUMSTANCES, BE LIABLE FOR DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES, FOR ANY REASON
 * WHATSOEVER.
 * ===========================================================================
 */

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "FreeRTOS_CLI.h"

#include "cli_task.h"

#include "xensiv_radar_presence.h"
#include "my_lidar_inf.h"
#include "my_rgb.h"

/*******************************************************************************
 * Macros
 ********************************************************************************/
#define NUMBER_OF_COMMANDS (15)

/* Strings length */
#define MAX_INPUT_LENGTH  (50)
#define MAX_OUTPUT_LENGTH (350)

/* Strings for enable and disable */
#define ENABLE_STRING  ("enable")
#define DISABLE_STRING ("disable")

/* Max range min - max */
#define MAX_RANGE_MIN_LIMIT (0.66f)
#define MAX_RANGE_MAX_LIMIT (5.0f)

/* Macro threshold min - max */
#define MACRO_THRESHOLD_MIN_LIMIT (0.05f)
#define MACRO_THRESHOLD_MAX_LIMIT (2.0f)

/* Micro threshold min - max */
#define MICRO_THRESHOLD_MIN_LIMIT (0.02f)
#define MICRO_THRESHOLD_MAX_LIMIT (50.0f)

/* Names for presence mode */
#define MACRO_ONLY_STRING      ("macro_only")
#define MICRO_ONLY_STRING      ("micro_only")
#define MICRO_IF_MACRO_STRING  ("micro_if_macro")
#define MICRO_AND_MACRO_STRING ("micro_and_macro")

/* Keyboard keys */
#define ENTER_KEY     (0x0D)
#define ESC_KEY       (0x1B)
#define BACKSPACE_KEY (0x08)

/*******************************************************************************
 * Function Prototypes
 ********************************************************************************/
static BaseType_t set_max_range(char *pcWriteBuffer, size_t xWriteBufferLen,
        const char *pcCommandString);
static BaseType_t set_macro_threshold(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString);
static BaseType_t set_micro_threshold(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString);
static BaseType_t turn_bandpass_filter(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString);
static BaseType_t turn_decimation_filter(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString);
static BaseType_t set_presence_mode(char *pcWriteBuffer, size_t xWriteBufferLen,
        const char *pcCommandString);
static BaseType_t cli_set_peak_height(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString);
static BaseType_t cli_set_phase_th(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString);
static BaseType_t cli_set_ampcv_th(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString);
static BaseType_t cli_set_binspan_th(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString);
static BaseType_t cli_set_confidence_th(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString);
static BaseType_t cli_set_miss_limit(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString);
static BaseType_t cli_presence_debug(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString);
static BaseType_t cli_show_presence(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString);
static BaseType_t cli_rgb(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString);
static inline bool check_bool_validation(const char *value, const char *enable,
        const char *disable);
static inline bool check_float_validation(float32_t value, float32_t min,
        float32_t max);
static inline bool string_to_bool(const char *string, const char *enable,
        const char *disable);
static inline bool check_mode_validation(const char *mode);
static inline xensiv_radar_presence_mode_t string_to_mode(const char *mode);
extern void presence_detection_cb(xensiv_radar_presence_handle_t handle,
        const xensiv_radar_presence_event_t *event, void *data);

/*******************************************************************************
 * Variables
 ********************************************************************************/
static const CLI_Command_Definition_t command_list[NUMBER_OF_COMMANDS] =
        {
                { .pcCommand = "set_max_range",
                        .pcHelpString =
                                "set_max_range <value> - Sets the max range for presence algorithm in meters. Range <0.66-5.0>\n",
                        .pxCommandInterpreter = set_max_range,
                        .cExpectedNumberOfParameters = 1 },
                { .pcCommand = "set_macro_threshold",
                        .pcHelpString =
                                "set_macro_threshold <value> - Sets macro threshold for presence algorithm. Range <0.05-2.0>\n",
                        .pxCommandInterpreter = set_macro_threshold,
                        .cExpectedNumberOfParameters = 1 },
                { .pcCommand = "set_micro_threshold",
                        .pcHelpString =
                                "set_micro_threshold <value> - Sets micro threshold for presence algorithm. Range <0.02-50.0>\n",
                        .pxCommandInterpreter = set_micro_threshold,
                        .cExpectedNumberOfParameters = 1 },
                { .pcCommand = "bandpass_filter",
                        .pcHelpString =
                                "bandpass_filter <enable|disable> - Enabling/disabling bandpass filter\n",
                        .pxCommandInterpreter = turn_bandpass_filter,
                        .cExpectedNumberOfParameters = 1 },
                { .pcCommand = "decimation_filter",
                        .pcHelpString =
                                "decimation_filter <enable|disable> - Enabling/disabling decimation filter\n",
                        .pxCommandInterpreter = turn_decimation_filter,
                        .cExpectedNumberOfParameters = 1 },
                { .pcCommand = "set_mode",
                        .pcHelpString =
                                "set_mode <macro_only|micro_only|micro_if_macro|micro_and_macro> - Chooses work mode\n",
                        .pxCommandInterpreter = set_presence_mode,
                        .cExpectedNumberOfParameters = 1 },
                { .pcCommand = "set_peak_height",
                        .pcHelpString =
                                "set_peak_height <value> - Breath peak height threshold. Range <0.005-0.50>\n",
                        .pxCommandInterpreter = cli_set_peak_height,
                        .cExpectedNumberOfParameters = 1 },
                { .pcCommand = "set_phase_th",
                        .pcHelpString =
                                "set_phase_th <value> - Phase excursion threshold in mm. Range <0.01-1.0>\n",
                        .pxCommandInterpreter = cli_set_phase_th,
                        .cExpectedNumberOfParameters = 1 },
                { .pcCommand = "set_ampcv_th",
                        .pcHelpString =
                                "set_ampcv_th <value> - Amplitude CV threshold. Range <0.05-2.0>\n",
                        .pxCommandInterpreter = cli_set_ampcv_th,
                        .cExpectedNumberOfParameters = 1 },
                { .pcCommand = "set_binspan_th",
                        .pcHelpString =
                                "set_binspan_th <value> - Bin span threshold. Range <0.5-10.0>\n",
                        .pxCommandInterpreter = cli_set_binspan_th,
                        .cExpectedNumberOfParameters = 1 },
                { .pcCommand = "set_confidence_th",
                        .pcHelpString =
                                "set_confidence_th <value> - Confidence threshold. Range <0.10-0.95>\n",
                        .pxCommandInterpreter = cli_set_confidence_th,
                        .cExpectedNumberOfParameters = 1 },
                { .pcCommand = "set_miss_limit",
                        .pcHelpString =
                                "set_miss_limit <value> - Latch miss frame limit. Range <0-20>\n",
                        .pxCommandInterpreter = cli_set_miss_limit,
                        .cExpectedNumberOfParameters = 1 },
                { .pcCommand = "presence_debug",
                        .pcHelpString =
                                "presence_debug <enable|disable> - Toggle 10Hz sub-indicator debug log\n",
                        .pxCommandInterpreter = cli_presence_debug,
                        .cExpectedNumberOfParameters = 1 },
                { .pcCommand = "show_presence",
                        .pcHelpString =
                                "show_presence - Show all presence threshold values\n",
                        .pxCommandInterpreter = cli_show_presence,
                        .cExpectedNumberOfParameters = 0 },
                { .pcCommand = "rgb",
                        .pcHelpString =
                                "rgb <R> <G> <B> <BRIGHTNESS> - Set LED color (0-255) and brightness (0-100)\n",
                        .pxCommandInterpreter = cli_rgb,
                        .cExpectedNumberOfParameters = 4 } };

static xensiv_radar_presence_handle_t handle;

/*******************************************************************************
 * Function Name: console_task
 ********************************************************************************
 * Summary:
 * This is the console task.
 *    1. Register commands
 *    2. In loop there are two modes: presence when the events are detected and setting_mode, when the user can change
 *work parameters
 *       - Waits for a sign
 *       - When ENTER is hit go to the settings mode
 *       - Use 'help' command to know what commands are available
 *       - Type commands with values to change parameters
 *       - Press ESC to exit settings mode and go again to presence mode
 *
 * Parameters:
 *  void
 *
 * Return:
 *  None
 *
 *******************************************************************************/
void console_task(void *pvParameters) {
    int8_t cInputIndex = 0;
    BaseType_t xMoreDataToFollow;
    /* The input and output buffers are declared static to keep them off the stack. */
    static char pcOutputString[MAX_OUTPUT_LENGTH];
    static char pcInputString[MAX_INPUT_LENGTH];
    bool setting_mode = false;

    handle = (xensiv_radar_presence_handle_t) pvParameters;
    /* handle 可以为 NULL — 旧 xensiv 命令不可用，新 presence 校准命令仍可工作 */

    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    for (int32_t i = 0; i < NUMBER_OF_COMMANDS; ++i) {
        FreeRTOS_CLIRegisterCommand(&command_list[i]);
    }

    for (;;) {
        int32_t c = getchar();

        if (c == EOF)
        {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        
        if (c == '\n')
        {
            c = ENTER_KEY;
        }
        

        if (!setting_mode) {
            /* Enter setting mode */
            if (c == ENTER_KEY) {
                cInputIndex = 0;
                memset(pcInputString, 0x00, MAX_INPUT_LENGTH);
                setting_mode = true;
                if (handle != NULL) {
                    xensiv_radar_presence_set_callback(handle, NULL, NULL);
                }
                printf("\r\nEnter setting mode\r\n"
                        "> ");
            }
        } else {
            /* Exit setting mode */
            if (c == ESC_KEY) {
                cInputIndex = 0;
                memset(pcInputString, 0x00, MAX_INPUT_LENGTH);
                setting_mode = false;
                printf(
                        "\r\nQuit from settings menu and back to processing\r\n\n");
                if (handle != NULL) {
                    xensiv_radar_presence_set_callback(handle,
                            presence_detection_cb, NULL);
                }
            } else if (c == ENTER_KEY) // confirm entered text
            {
                putchar('\r');
                putchar('\n');

                /* The command interpreter is called repeatedly until it returns
                 pdFALSE.  See the "Implementing a command" documentation for an
                 explanation of why this is. */
                do {
                    /* Send the command string to the command interpreter.  Any
                     output generated by the command interpreter will be placed in the
                     pcOutputString buffer. */
                    xMoreDataToFollow = FreeRTOS_CLIProcessCommand(
                            pcInputString, /* The command string.*/
                            pcOutputString, /* The output buffer. */
                            MAX_OUTPUT_LENGTH /* The size of the output buffer. */
                            );

                    /* Write the output generated by the command interpreter to the
                     console. */
                    printf("%s", pcOutputString);
                } while (xMoreDataToFollow != pdFALSE);

                /* All the strings generated by the input command have been sent.
                 Processing of the command is complete.  Clear the input string ready
                 to receive the next command. */

                cInputIndex = 0;
                memset(pcInputString, 0x00, MAX_INPUT_LENGTH);

                printf("> ");
            } else {
                if (c == BACKSPACE_KEY) {
                    /* Backspace was pressed.  Erase the last character in the input
                     buffer - if there are any. */
                    if (cInputIndex > 0) {
                        cInputIndex--;
                        pcInputString[cInputIndex] = ' ';
                        putchar(BACKSPACE_KEY);
                    }
                } else {
                    /* A character was entered.  It was not a new line, backspace
                     or carriage return, so it is accepted as part of the input and
                     placed into the input buffer.  When a \n is entered the complete
                     string will be passed to the command interpreter. */
                    if (cInputIndex < MAX_INPUT_LENGTH) {
                        pcInputString[cInputIndex] = c;
                        cInputIndex++;
                        putchar(c);
                    }
                }
            }
        }
    }
}

/*******************************************************************************
 * Function Name: set_max_range
 ********************************************************************************
 * Summary:
 *   Sets the max range for presence detection algorithm
 *
 * Parameters:
 *   pcWriteBuffer: buffer into which the output from executing the command can be written
 *   xWriteBufferLen:length, in bytes of the pcWriteBuffer buffer
 *   pcCommandString: entire string as input by
 the user (from which parameters can be extracted)
 *
 * Return:
 *   pdFALSE indicating that the function ends it's processing
 *******************************************************************************/
/* 旧 xensiv 命令需要 handle 有效 */
#define CLI_REQUIRE_HANDLE(buf)                                               \
    do {                                                                       \
        if (handle == NULL) {                                                  \
            snprintf((buf), xWriteBufferLen,                                   \
                     "Error: xensiv handle not available\r\n\n");              \
            return pdFALSE;                                                    \
        }                                                                      \
    } while (0)

static BaseType_t set_max_range(char *pcWriteBuffer, size_t xWriteBufferLen,
        const char *pcCommandString) {
    int32_t result = XENSIV_RADAR_PRESENCE_OK;
    float32_t float_value = 0.0;
    xensiv_radar_presence_config_t config;
    const char *pcParameter;
    BaseType_t lParameterStringLength;

    configASSERT(pcWriteBuffer);
    CLI_REQUIRE_HANDLE(pcWriteBuffer);

    /* Obtain the parameter string. */
    pcParameter = FreeRTOS_CLIGetParameter(pcCommandString, /* The command string itself. */
    1, /* Return the first parameter. */
    &lParameterStringLength); /* Store the parameter string length. */

    configASSERT(pcParameter);
    result = xensiv_radar_presence_get_config(handle, &config);

    if (result != XENSIV_RADAR_PRESENCE_OK) {
        sprintf(pcWriteBuffer, "Error while reading presence config\r\n");
    } else {
        float_value = strtof(pcParameter, NULL);

        if (check_float_validation(float_value, MAX_RANGE_MIN_LIMIT,
                MAX_RANGE_MAX_LIMIT)) {

            config.max_range_bin = (int32_t) (float_value
                    / (xensiv_radar_presence_get_bin_length(handle)));
            vTaskSuspendAll();
            result = xensiv_radar_presence_set_config(handle, &config);
            xensiv_radar_presence_reset(handle);
            xTaskResumeAll();

            if (result != XENSIV_RADAR_PRESENCE_OK) {
                sprintf(pcWriteBuffer, "Error while setting new config.\r\n\n");
            } else {
                sprintf(pcWriteBuffer, "ok\n");
            }
        } else {
            sprintf(pcWriteBuffer, "Invalid value.\r\n\n");
        }
    }

    return pdFALSE;
}

/*******************************************************************************
 * Function Name: set_macro_threshold
 ********************************************************************************
 * Summary:
 *   Sets macro threshold for presence detection algorithm
 *
 * Parameters:
 *   pcWriteBuffer: buffer into which the output from executing the command can be written
 *   xWriteBufferLen:length, in bytes of the pcWriteBuffer buffer
 *   pcCommandString: entire string as input by
 the user (from which parameters can be extracted)
 *
 * Return:
 *   pdFALSE indicating that the function ends it's processing
 *******************************************************************************/
static BaseType_t set_macro_threshold(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString) {
    int32_t result = XENSIV_RADAR_PRESENCE_OK;
    float32_t float_value = 0.0;
    xensiv_radar_presence_config_t config;
    const char *pcParameter;
    BaseType_t lParameterStringLength;

    configASSERT(pcWriteBuffer);
    CLI_REQUIRE_HANDLE(pcWriteBuffer);

    /* Obtain the parameter string. */
    pcParameter = FreeRTOS_CLIGetParameter(pcCommandString, /* The command string itself. */
    1, /* Return the first parameter. */
    &lParameterStringLength); /* Store the parameter string length. */

    configASSERT(pcParameter);
    result = xensiv_radar_presence_get_config(handle, &config);

    if (result != XENSIV_RADAR_PRESENCE_OK) {
        sprintf(pcWriteBuffer, "Error while reading presence config\r\n");
    } else {
        float_value = strtof(pcParameter, NULL);

        if (check_float_validation(float_value, MACRO_THRESHOLD_MIN_LIMIT,
                MACRO_THRESHOLD_MAX_LIMIT)) {
            config.macro_threshold = float_value;
            vTaskSuspendAll();
            result = xensiv_radar_presence_set_config(handle, &config);
            xensiv_radar_presence_reset(handle);
            xTaskResumeAll();

            if (result != XENSIV_RADAR_PRESENCE_OK) {
                sprintf(pcWriteBuffer, "Error while setting new config.\r\n\n");
            } else {
                sprintf(pcWriteBuffer, "ok\n");
            }
        } else {
            sprintf(pcWriteBuffer, "Invalid value.\r\n\n");
        }
    }

    return pdFALSE;
}

/*******************************************************************************
 * Function Name: set_micro_threshold
 ********************************************************************************
 * Summary:
 *   Sets micro threshold for presence detection algorithm
 *
 * Parameters:
 *   pcWriteBuffer: buffer into which the output from executing the command can be written
 *   xWriteBufferLen:length, in bytes of the pcWriteBuffer buffer
 *   pcCommandString: entire string as input by
 the user (from which parameters can be extracted)
 *
 * Return:
 *   pdFALSE indicating that the function ends it's processing
 *******************************************************************************/
static BaseType_t set_micro_threshold(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString) {
    int32_t result = XENSIV_RADAR_PRESENCE_OK;
    float32_t float_value = 0.0;
    xensiv_radar_presence_config_t config;
    const char *pcParameter;
    BaseType_t lParameterStringLength;

    configASSERT(pcWriteBuffer);
    CLI_REQUIRE_HANDLE(pcWriteBuffer);

    /* Obtain the parameter string. */
    pcParameter = FreeRTOS_CLIGetParameter(pcCommandString, /* The command string itself. */
    1, /* Return the first parameter. */
    &lParameterStringLength); /* Store the parameter string length. */

    configASSERT(pcParameter);
    result = xensiv_radar_presence_get_config(handle, &config);

    if (result != XENSIV_RADAR_PRESENCE_OK) {
        sprintf(pcWriteBuffer, "Error while reading presence config\r\n");
    } else {
        float_value = strtof(pcParameter, NULL);

        if (check_float_validation(float_value, MICRO_THRESHOLD_MIN_LIMIT,
                MICRO_THRESHOLD_MAX_LIMIT)) {
            config.micro_threshold = float_value;
            vTaskSuspendAll();
            result = xensiv_radar_presence_set_config(handle, &config);
            xensiv_radar_presence_reset(handle);
            xTaskResumeAll();

            if (result != XENSIV_RADAR_PRESENCE_OK) {
                sprintf(pcWriteBuffer, "Error while setting new config.\r\n\n");
            } else {
                sprintf(pcWriteBuffer, "ok\n");
            }
        } else {
            sprintf(pcWriteBuffer, "Invalid value.\r\n\n");
        }
    }

    return pdFALSE;
}

/*******************************************************************************
 * Function Name: turn_bandpass_filter
 ********************************************************************************
 * Summary:
 *   Turning on/off bandpass filter for presence detection algorithm
 *
 * Parameters:
 *   pcWriteBuffer: buffer into which the output from executing the command can be written
 *   xWriteBufferLen:length, in bytes of the pcWriteBuffer buffer
 *   pcCommandString: entire string as input by
 the user (from which parameters can be extracted)
 *
 * Return:
 *   pdFALSE indicating that the function ends it's processing
 *******************************************************************************/
static BaseType_t turn_bandpass_filter(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString) {
    int32_t result = XENSIV_RADAR_PRESENCE_OK;
    xensiv_radar_presence_config_t config;
    const char *pcParameter;
    BaseType_t lParameterStringLength;

    configASSERT(pcWriteBuffer);
    CLI_REQUIRE_HANDLE(pcWriteBuffer);

    /* Obtain the parameter string. */
    pcParameter = FreeRTOS_CLIGetParameter(pcCommandString, /* The command string itself. */
    1, /* Return the first parameter. */
    &lParameterStringLength); /* Store the parameter string length. */

    configASSERT(pcParameter);
    result = xensiv_radar_presence_get_config(handle, &config);

    if (result != XENSIV_RADAR_PRESENCE_OK) {
        sprintf(pcWriteBuffer, "Error while reading presence config\r\n");
    } else {
        if (check_bool_validation(pcParameter, ENABLE_STRING, DISABLE_STRING)) {
            config.macro_fft_bandpass_filter_enabled = string_to_bool(
                    pcParameter, ENABLE_STRING, DISABLE_STRING);
            vTaskSuspendAll();
            result = xensiv_radar_presence_set_config(handle, &config);
            xensiv_radar_presence_reset(handle);
            xTaskResumeAll();

            if (result != XENSIV_RADAR_PRESENCE_OK) {
                sprintf(pcWriteBuffer, "Error while setting new config.\r\n\n");
            } else {
                sprintf(pcWriteBuffer, "ok\n");
            }
        } else {
            sprintf(pcWriteBuffer, "Invalid value.\r\n\n");
        }
    }

    return pdFALSE;
}

/*******************************************************************************
 * Function Name: turn_decimation_filter
 ********************************************************************************
 * Summary:
 *   Turning on/off decimation filter for presence detection algorithm
 *
 * Parameters:
 *   pcWriteBuffer: buffer into which the output from executing the command can be written
 *   xWriteBufferLen:length, in bytes of the pcWriteBuffer buffer
 *   pcCommandString: entire string as input by
 the user (from which parameters can be extracted)
 *
 * Return:
 *   pdFALSE indicating that the function ends it's processing
 *******************************************************************************/
static BaseType_t turn_decimation_filter(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString) {
    int32_t result = XENSIV_RADAR_PRESENCE_OK;
    xensiv_radar_presence_config_t config;
    const char *pcParameter;
    BaseType_t lParameterStringLength;

    configASSERT(pcWriteBuffer);
    CLI_REQUIRE_HANDLE(pcWriteBuffer);

    /* Obtain the parameter string. */
    pcParameter = FreeRTOS_CLIGetParameter(pcCommandString, /* The command string itself. */
    1, /* Return the first parameter. */
    &lParameterStringLength); /* Store the parameter string length. */

    configASSERT(pcParameter);
    result = xensiv_radar_presence_get_config(handle, &config);

    if (result != XENSIV_RADAR_PRESENCE_OK) {
        sprintf(pcWriteBuffer, "Error while reading presence config\r\n");
    } else {
        if (check_bool_validation(pcParameter, ENABLE_STRING, DISABLE_STRING)) {
            config.micro_fft_decimation_enabled = string_to_bool(pcParameter,
                    ENABLE_STRING, DISABLE_STRING);
            vTaskSuspendAll();
            result = xensiv_radar_presence_set_config(handle, &config);
            xensiv_radar_presence_reset(handle);
            xTaskResumeAll();

            if (result != XENSIV_RADAR_PRESENCE_OK) {
                sprintf(pcWriteBuffer, "Error while setting new config.\r\n\n");
            } else {
                sprintf(pcWriteBuffer, "ok\n");
            }
        } else {
            sprintf(pcWriteBuffer, "Invalid value.\r\n\n");
        }
    }

    return pdFALSE;
}

/*******************************************************************************
 * Function Name: set_presence_mode
 ********************************************************************************
 * Summary:
 *   Setting mode for presence detection algorithm
 *
 * Parameters:
 *   pcWriteBuffer: buffer into which the output from executing the command can be written
 *   xWriteBufferLen:length, in bytes of the pcWriteBuffer buffer
 *   pcCommandString: entire string as input by
 the user (from which parameters can be extracted)
 *
 * Return:
 *   pdFALSE indicating that the function ends it's processing
 *******************************************************************************/
static BaseType_t set_presence_mode(char *pcWriteBuffer, size_t xWriteBufferLen,
        const char *pcCommandString) {
    int32_t result = XENSIV_RADAR_PRESENCE_OK;
    xensiv_radar_presence_config_t config;
    const char *pcParameter;
    BaseType_t lParameterStringLength;

    configASSERT(pcWriteBuffer);
    CLI_REQUIRE_HANDLE(pcWriteBuffer);

    /* Obtain the parameter string. */
    pcParameter = FreeRTOS_CLIGetParameter(pcCommandString, /* The command string itself. */
    1, /* Return the first parameter. */
    &lParameterStringLength); /* Store the parameter string length. */

    configASSERT(pcParameter);
    result = xensiv_radar_presence_get_config(handle, &config);

    if (result != XENSIV_RADAR_PRESENCE_OK) {
        sprintf(pcWriteBuffer, "Error while reading presence config\r\n");
    } else {
        if (check_mode_validation(pcParameter)) {
            xensiv_radar_presence_mode_t mode = string_to_mode(pcParameter);

            config.mode = mode;
            vTaskSuspendAll();
            result = xensiv_radar_presence_set_config(handle, &config);
            xensiv_radar_presence_reset(handle);
            xTaskResumeAll();

            if (result != XENSIV_RADAR_PRESENCE_OK) {
                sprintf(pcWriteBuffer, "Error while setting new config.\r\n\n");
            } else {
                sprintf(pcWriteBuffer, "ok\n");
            }
        } else {
            sprintf(pcWriteBuffer, "Invalid value.\r\n\n");
        }
    }

    return pdFALSE;
}

/*******************************************************************************
 * Function Name: check_bool_validation
 ********************************************************************************
 * Summary:
 *   Checks if entered value is a proper string for enable or disable
 *
 * Parameters:
 *   value : Entered string value
 *   enable : String value for enable
 *   disable : String value for disable
 *
 * Return:
 *   True if the value is same as enable or disable value, false if it is different
 *******************************************************************************/
static inline bool check_bool_validation(const char *value, const char *enable,
        const char *disable) {
    bool result = false;

    if ((strcmp(value, enable) == 0) || (strcmp(value, disable) == 0)) {
        result = true;
    } else {
        result = false;
    }

    return result;
}

/*******************************************************************************
 * Function Name: check_float_validation
 ********************************************************************************
 * Summary:
 *   Checks if entered value is within a range
 *
 * Parameters:
 *   value : Entered float value
 *   min : Minimum value
 *   max : Maximum value
 *
 * Return:
 *   True if the value is within a range, false if not
 *******************************************************************************/
static inline bool check_float_validation(float32_t value, float32_t min,
        float32_t max) {
    bool result = false;

    if ((value >= min) && (value <= max)) {
        result = true;
    } else {
        result = false;
    }

    return result;
}

/*******************************************************************************
 * Function Name: string_to_bool
 ********************************************************************************
 * Summary:
 *   Converts string value to true or false
 *
 * Parameters:
 *   string : Entered string value
 *   enable : String for enable value
 *   disable : String for disable value
 *
 * Return:
 *   True if entered string is enable, false if disable
 *******************************************************************************/
static inline bool string_to_bool(const char *string, const char *enable,
        const char *disable) {
    assert((strcmp(string, enable) == 0) || (strcmp(string, disable) == 0));
    bool result = false;

    if (strcmp(string, enable) == 0) {
        result = true;
    } else {
        result = false;
    }

    return result;
}

/*******************************************************************************
 * Function Name: check_mode_validation
 ********************************************************************************
 * Summary:
 *   Checks if entered value is a proper mode value
 *
 * Parameters:
 *   value : Entered mode value
 *
 * Return:
 *   True if the value is correct, false if the value is incorrect
 *******************************************************************************/
static inline bool check_mode_validation(const char *mode) {
    bool result = false;

    if ((strcmp(mode, MACRO_ONLY_STRING) == 0)
            || (strcmp(mode, MICRO_ONLY_STRING) == 0)
            || (strcmp(mode, MICRO_IF_MACRO_STRING) == 0)
            || (strcmp(mode, MICRO_AND_MACRO_STRING) == 0)) {
        result = true;
    } else {
        result = false;
    }

    return result;
}

/*******************************************************************************
 * Function Name: string_to_mode
 ********************************************************************************
 * Summary:
 *   Translates string value into a numeral value for mode
 *
 * Parameters:
 *   value : Entered string
 *
 * Return:
 *   Presence mode value
 *******************************************************************************/
static inline xensiv_radar_presence_mode_t string_to_mode(const char *mode) {
    xensiv_radar_presence_mode_t result = XENSIV_RADAR_PRESENCE_MODE_MACRO_ONLY;

    if (strcmp(mode, MACRO_ONLY_STRING) == 0) {
        result = XENSIV_RADAR_PRESENCE_MODE_MACRO_ONLY;
    } else if (strcmp(mode, MICRO_ONLY_STRING) == 0) {
        result = XENSIV_RADAR_PRESENCE_MODE_MICRO_ONLY;
    } else if (strcmp(mode, MICRO_IF_MACRO_STRING) == 0) {
        result = XENSIV_RADAR_PRESENCE_MODE_MICRO_IF_MACRO;
    } else if (strcmp(mode, MICRO_AND_MACRO_STRING) == 0) {
        result = XENSIV_RADAR_PRESENCE_MODE_MICRO_AND_MACRO;
    } else {
    }

    return result;
}


/*******************************************************************************
 * Presence 校准 CLI 命令 — 通用 float 阈值设置辅助宏
 ********************************************************************************/
#define CLI_SET_PRESENCE_FLOAT(field, vmin, vmax)                              \
    do {                                                                       \
        const char *p;                                                         \
        BaseType_t plen;                                                       \
        configASSERT(pcWriteBuffer);                                           \
        p = FreeRTOS_CLIGetParameter(pcCommandString, 1, &plen);               \
        configASSERT(p);                                                       \
        float32_t v = strtof(p, NULL);                                         \
        if (check_float_validation(v, (vmin), (vmax))) {                       \
            radar_presence_thresholds_t th;                                    \
            radar_presence_get_thresholds(&th);                                \
            th.field = v;                                                      \
            vTaskSuspendAll();                                                 \
            radar_presence_set_thresholds(&th);                                \
            xTaskResumeAll();                                                  \
            snprintf(pcWriteBuffer, xWriteBufferLen,                           \
                     "ok (" #field "=%.4f)\n", (double)v);                     \
        } else {                                                               \
            snprintf(pcWriteBuffer, xWriteBufferLen,                           \
                     "Invalid value. Range: %.3f - %.3f\r\n\n",               \
                     (double)(vmin), (double)(vmax));                          \
        }                                                                      \
    } while (0)

static BaseType_t cli_set_peak_height(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString) {
    CLI_SET_PRESENCE_FLOAT(peak_height, 0.005f, 0.50f);
    return pdFALSE;
}

static BaseType_t cli_set_phase_th(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString) {
    CLI_SET_PRESENCE_FLOAT(phase_exc_mm_th, 0.01f, 1.0f);
    return pdFALSE;
}

static BaseType_t cli_set_ampcv_th(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString) {
    CLI_SET_PRESENCE_FLOAT(amp_cv_th, 0.05f, 2.0f);
    return pdFALSE;
}

static BaseType_t cli_set_binspan_th(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString) {
    CLI_SET_PRESENCE_FLOAT(bin_span_th, 0.5f, 10.0f);
    return pdFALSE;
}

static BaseType_t cli_set_confidence_th(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString) {
    CLI_SET_PRESENCE_FLOAT(confidence_th, 0.10f, 0.95f);
    return pdFALSE;
}

static BaseType_t cli_set_miss_limit(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString) {
    const char *p;
    BaseType_t plen;
    configASSERT(pcWriteBuffer);
    p = FreeRTOS_CLIGetParameter(pcCommandString, 1, &plen);
    configASSERT(p);
    int32_t v = (int32_t)strtol(p, NULL, 10);
    if (v >= 0 && v <= 20) {
        radar_presence_thresholds_t th;
        radar_presence_get_thresholds(&th);
        th.miss_limit = (uint32_t)v;
        vTaskSuspendAll();
        radar_presence_set_thresholds(&th);
        xTaskResumeAll();
        snprintf(pcWriteBuffer, xWriteBufferLen,
                 "ok (miss_limit=%" PRIi32 ")\n", v);
    } else {
        snprintf(pcWriteBuffer, xWriteBufferLen,
                 "Invalid value. Range: 0 - 20\r\n\n");
    }
    return pdFALSE;
}

static BaseType_t cli_presence_debug(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString) {
    const char *p;
    BaseType_t plen;
    configASSERT(pcWriteBuffer);
    p = FreeRTOS_CLIGetParameter(pcCommandString, 1, &plen);
    configASSERT(p);
    if (check_bool_validation(p, ENABLE_STRING, DISABLE_STRING)) {
        radar_presence_thresholds_t th;
        radar_presence_get_thresholds(&th);
        th.debug_log_enabled = string_to_bool(p, ENABLE_STRING, DISABLE_STRING);
        vTaskSuspendAll();
        radar_presence_set_thresholds(&th);
        xTaskResumeAll();
        snprintf(pcWriteBuffer, xWriteBufferLen,
                 "ok (presence_debug=%s)\n",
                 th.debug_log_enabled ? "enabled" : "disabled");
    } else {
        snprintf(pcWriteBuffer, xWriteBufferLen, "Invalid value.\r\n\n");
    }
    return pdFALSE;
}

static BaseType_t cli_show_presence(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString) {
    (void)pcCommandString;
    configASSERT(pcWriteBuffer);
    radar_presence_thresholds_t th;
    radar_presence_get_thresholds(&th);
    snprintf(pcWriteBuffer, xWriteBufferLen,
             "Presence thresholds:\r\n"
             "  peak_height    = %.4f\r\n"
             "  phase_exc_th   = %.4f mm\r\n"
             "  amp_cv_th      = %.4f\r\n"
             "  bin_span_th    = %.1f\r\n"
             "  confidence_th  = %.2f\r\n"
             "  miss_limit     = %" PRIu32 "\r\n"
             "  debug_log      = %s\r\n",
             (double)th.peak_height,
             (double)th.phase_exc_mm_th,
             (double)th.amp_cv_th,
             (double)th.bin_span_th,
             (double)th.confidence_th,
             th.miss_limit,
             th.debug_log_enabled ? "enabled" : "disabled");
    return pdFALSE;
}

/* rgb R G B BRIGHTNESS */
static BaseType_t cli_rgb(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString)
{
    const char *p;
    BaseType_t plen;
    configASSERT(pcWriteBuffer);

    p = FreeRTOS_CLIGetParameter(pcCommandString, 1, &plen);
    if (!p) { snprintf(pcWriteBuffer, xWriteBufferLen, "Err\n"); return pdFALSE; }
    int32_t r = (int32_t)strtol(p, NULL, 10);

    p = FreeRTOS_CLIGetParameter(pcCommandString, 2, &plen);
    if (!p) { snprintf(pcWriteBuffer, xWriteBufferLen, "Err\n"); return pdFALSE; }
    int32_t g = (int32_t)strtol(p, NULL, 10);

    p = FreeRTOS_CLIGetParameter(pcCommandString, 3, &plen);
    if (!p) { snprintf(pcWriteBuffer, xWriteBufferLen, "Err\n"); return pdFALSE; }
    int32_t b = (int32_t)strtol(p, NULL, 10);

    p = FreeRTOS_CLIGetParameter(pcCommandString, 4, &plen);
    if (!p) { snprintf(pcWriteBuffer, xWriteBufferLen, "Err\n"); return pdFALSE; }
    int32_t br = (int32_t)strtol(p, NULL, 10);

    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    if (br < 0) br = 0; else if (br > 100) br = 100;

    my_rgb_set_color((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)br);
    snprintf(pcWriteBuffer, xWriteBufferLen, "ok rgb=%" PRIi32 ",%" PRIi32 ",%" PRIi32 " br=%" PRIi32 "\n",
             r, g, b, br);
    return pdFALSE;
}

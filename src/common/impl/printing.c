#include "fastfetch.h"
#include "common/printing.h"
#include "common/textModifier.h"
#include "logo/logo.h"
#include <ctype.h>
#include <stdlib.h>

typedef struct FFRenderRow {
    FFstrbuf output;
    size_t connectorOffset;
    bool hasConnector;
} FFRenderRow;

static FFlist* s_capturedRows = NULL;
static FILE* s_capturedStream = NULL;
static char* s_capturedBuf = NULL;
static size_t s_capturedSize = 0;
static FILE* s_oldStdout = NULL;
static size_t s_currentRowConnectorOffset = 0;
static bool s_currentRowHasConnector = false;

static const char* find_leading_tree_connector(const char* key_str) {
    if (!key_str) return NULL;
    while (isspace((unsigned char)*key_str)) {
        key_str++;
    }
    if ((unsigned char)key_str[0] == 0xE2 &&
        (unsigned char)key_str[1] == 0x94 &&
        ((unsigned char)key_str[2] == 0x9C || (unsigned char)key_str[2] == 0x94)) {
        return key_str;
    }
    return NULL;
}

static void endCapturedRow(void) {
    if (s_capturedStream) {
        fflush(s_capturedStream);
        stdout = s_oldStdout;
        s_oldStdout = NULL;

        FFRenderRow* row = (FFRenderRow*) ffListAdd(s_capturedRows, sizeof(FFRenderRow));
        ffStrbufInit(&row->output);
        ffStrbufSetNS(&row->output, (uint32_t) s_capturedSize, s_capturedBuf);
        row->hasConnector = s_currentRowHasConnector;
        row->connectorOffset = s_currentRowConnectorOffset;

        fclose(s_capturedStream);
        free(s_capturedBuf);
        s_capturedStream = NULL;
        s_capturedBuf = NULL;
        s_capturedSize = 0;
    }
}

static void ffStartCapturedRow(void) {
    if (s_capturedStream) {
        endCapturedRow();
    }

    if (!s_capturedRows) {
        s_capturedRows = (FFlist*) malloc(sizeof(FFlist));
        ffListInit(s_capturedRows);
    }

    s_capturedStream = open_memstream(&s_capturedBuf, &s_capturedSize);
    if (s_capturedStream) {
        s_oldStdout = stdout;
        stdout = s_capturedStream;
        s_currentRowConnectorOffset = 0;
        s_currentRowHasConnector = false;
    }
}

void ffRendererFinalizeTree(void) {
    if (!instance.config.general.treeConnectors) return;

    endCapturedRow();

    if (!s_capturedRows || s_capturedRows->length == 0) return;



    int lastConnectorIdx = -1;
    for (uint32_t i = 0; i < s_capturedRows->length; i++) {
        FFRenderRow* row = (FFRenderRow*) ffListGet(s_capturedRows, sizeof(FFRenderRow), i);
        if (row->hasConnector) {
            lastConnectorIdx = (int) i;
        }
    }

    for (uint32_t i = 0; i < s_capturedRows->length; i++) {
        FFRenderRow* row = (FFRenderRow*) ffListGet(s_capturedRows, sizeof(FFRenderRow), i);
        if (row->hasConnector) {
            if ((int) i == lastConnectorIdx) {
                if (row->connectorOffset + 2 < row->output.length) {
                    row->output.chars[row->connectorOffset] = (char) 0xE2;
                    row->output.chars[row->connectorOffset + 1] = (char) 0x94;
                    row->output.chars[row->connectorOffset + 2] = (char) 0x94;
                }
            } else {
                if (row->connectorOffset + 2 < row->output.length) {
                    row->output.chars[row->connectorOffset] = (char) 0xE2;
                    row->output.chars[row->connectorOffset + 1] = (char) 0x94;
                    row->output.chars[row->connectorOffset + 2] = (char) 0x9C;
                }
            }
        }
    }

    for (uint32_t i = 0; i < s_capturedRows->length; i++) {
        FFRenderRow* row = (FFRenderRow*) ffListGet(s_capturedRows, sizeof(FFRenderRow), i);
        ffStrbufWriteTo(&row->output, stdout);
        ffStrbufDestroy(&row->output);
    }

    ffListDestroy(s_capturedRows);
    free(s_capturedRows);
    s_capturedRows = NULL;
}

void ffPrintLogoAndKey(const char* moduleName, uint8_t moduleIndex, const FFModuleArgs* moduleArgs, FFPrintType printType) {
    if (instance.config.general.treeConnectors) {
        ffStartCapturedRow();
    }
    ffLogoPrintLine();

    // This is used by --set-keyless, in this case we want neither the module name nor the separator
    if (moduleName == nullptr) {
        return;
    }

    // This is used as a magic value for hiding keys
    if (!(moduleArgs && ffStrbufEqualS(&moduleArgs->key, " ")) && instance.config.display.keyType != FF_MODULE_KEY_TYPE_NONE) {
        ffPrintCharTimes(' ', instance.config.display.keyPaddingLeft);

        if (!instance.config.display.pipe) {
            fputs(FASTFETCH_TEXT_MODIFIER_RESET, stdout);
            if (instance.config.display.brightColor) {
                fputs(FASTFETCH_TEXT_MODIFIER_BOLT, stdout);
            }

            if (moduleArgs && !(printType & FF_PRINT_TYPE_NO_CUSTOM_KEY_COLOR) && moduleArgs->keyColor.length > 0) {
                ffPrintColor(&moduleArgs->keyColor);
            } else {
                ffPrintColor(&instance.config.display.colorKeys);
            }
        }

        if (instance.config.display.keyType & FF_MODULE_KEY_TYPE_ICON && moduleArgs && moduleArgs->keyIcon.length > 0) {
            ffStrbufWriteTo(&moduleArgs->keyIcon, stdout);
        }

        if (instance.config.display.keyType & FF_MODULE_KEY_TYPE_STRING) {
            ffPrintCharTimes(' ', instance.config.display.keyType >> FF_MODULE_KEY_TYPE_SPACE_SHIFT);

            // nullptr check is required for modules with custom keys, e.g. disk with the folder path
            if ((printType & FF_PRINT_TYPE_NO_CUSTOM_KEY) || !moduleArgs || moduleArgs->key.length == 0) {
                if (instance.config.general.treeConnectors && s_capturedStream) {
                    const char* conn = find_leading_tree_connector(moduleName);
                    if (conn) {
                        fflush(s_capturedStream);
                        s_currentRowHasConnector = true;
                        s_currentRowConnectorOffset = s_capturedSize + (size_t)(conn - moduleName);
                    }
                }
                fputs(moduleName, stdout);

                if (moduleIndex > 0) {
                    printf(" %hhu", moduleIndex);
                }
            } else {
                FF_STRBUF_AUTO_DESTROY key = ffStrbufCreate();
                FF_PARSE_FORMAT_STRING_CHECKED(&key, &moduleArgs->key, ((FFformatarg[]) {
                                                                           FF_ARG(moduleIndex, "index"),
                                                                           FF_ARG(moduleArgs->keyIcon, "icon"),
                                                                        }));
                if (instance.config.general.treeConnectors && s_capturedStream) {
                    const char* conn = find_leading_tree_connector(key.chars);
                    if (conn) {
                        fflush(s_capturedStream);
                        s_currentRowHasConnector = true;
                        s_currentRowConnectorOffset = s_capturedSize + (size_t)(conn - key.chars);
                    }
                }
                ffStrbufWriteTo(&key, stdout);
            }
        }

        if (!instance.config.display.pipe) {
            fputs(FASTFETCH_TEXT_MODIFIER_RESET, stdout);
            ffPrintColor(&instance.config.display.colorSeparator);
        }

        ffStrbufWriteTo(&instance.config.display.keyValueSeparator, stdout);

        if (!instance.config.display.pipe && instance.config.display.colorSeparator.length) {
            fputs(FASTFETCH_TEXT_MODIFIER_RESET, stdout);
        }

        if (!(printType & FF_PRINT_TYPE_NO_CUSTOM_KEY_WIDTH)) {
            uint32_t keyWidth = moduleArgs && moduleArgs->keyWidth > 0 ? moduleArgs->keyWidth : instance.config.display.keyWidth;
            if (keyWidth > 0) {
                printf("\e[%uG", (unsigned) (keyWidth + instance.state.logoWidth));
            }
        }
    }

    if (!instance.config.display.pipe) {
        fputs(FASTFETCH_TEXT_MODIFIER_RESET, stdout);
        if (moduleArgs && moduleArgs->outputColor.length) {
            ffPrintColor(&moduleArgs->outputColor);
        } else if (instance.config.display.colorOutput.length) {
            ffPrintColor(&instance.config.display.colorOutput);
        }
    }
}

bool ffPrintFormat(const char* moduleName, uint8_t moduleIndex, const FFModuleArgs* moduleArgs, FFPrintType printType, uint32_t numArgs, const FFformatarg* arguments) {
    FF_STRBUF_AUTO_DESTROY buffer = ffStrbufCreate();
    bool success;
    if (__builtin_expect(moduleArgs != nullptr, 1)) {
        success = ffParseFormatString(&buffer, &moduleArgs->outputFormat, numArgs, arguments);
    } else {
        ffStrbufSetStatic(&buffer, "undefined format");
        success = false;
    }

    if (success) {
        ffPrintLogoAndKey(moduleName, moduleIndex, moduleArgs, printType);
        ffStrbufPutTo(&buffer, stdout);
    } else {
        ffPrintError(moduleName, moduleIndex, moduleArgs, printType, "%s", buffer.chars);
    }

    return success;
}

void ffPrintError(const char* moduleName, uint8_t moduleIndex, const FFModuleArgs* moduleArgs, FFPrintType printType, const char* message, ...) {
    if (!instance.config.display.showErrors) {
        return;
    }

    ffPrintLogoAndKey(moduleName, moduleIndex, moduleArgs, printType);

    if (!instance.config.display.pipe) {
        fputs(FASTFETCH_TEXT_MODIFIER_ERROR, stdout);
    }

    va_list arguments;
    va_start(arguments, message);
    vprintf(message, arguments);
    va_end(arguments);

    if (!instance.config.display.pipe) {
        fputs(FASTFETCH_TEXT_MODIFIER_RESET, stdout);
    }

    putchar('\n');
}

void ffPrintColor(const FFstrbuf* colorValue) {
    // If the color is not set, this would reset in \033[m, which resets everything.
    // So we only print it, if the main color is at least one char.
    if (colorValue->length == 0) {
        return;
    }

    printf("\e[%sm", colorValue->chars);
}

void ffPrintCharTimes(char c, uint32_t times) {
    if (times == 0) {
        return;
    }

    if (times == 1) {
        putchar(c);
        return;
    }

    char str[32];
    memset(str, c, sizeof(str)); // 2 instructions when compiling with AVX2 enabled
    for (uint32_t i = sizeof(str); i <= times; i += (uint32_t) sizeof(str)) {
        fwrite(str, 1, sizeof(str), stdout);
    }
    uint32_t remaining = times % sizeof(str);
    if (remaining > 0) {
        fwrite(str, 1, remaining, stdout);
    }
}

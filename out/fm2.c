#include "fm2.h"

// Initialize the parser with an opened file
bool fm2_init(FM2Parser* parser, const char* filename) {
    if (!parser) return false;

    parser->file = fopen(filename, "r");
    if (!parser->file) {
        return false;
    }

    parser->header_parsed = false;
    return true;
}

// Skip the header section until we reach the input log
static void skip_header(FM2Parser* parser) {
    while (fgets(parser->line_buffer, MAX_LINE_LENGTH, parser->file)) {
        if (parser->line_buffer[0] == '|') {
            // Cast strlen result to long for fseek
            long offset = -(long)strlen(parser->line_buffer);
            fseek(parser->file, offset, SEEK_CUR);
            parser->header_parsed = true;
            return;
        }
    }
}

// Convert controller string to bitfield
static uint8_t parse_controller_input(const char* input) {
    uint8_t state = 0;
    
    // Each position corresponds to a button
    if (input[0] != '.' && input[0] != ' ') state |= CONTROLLER_RIGHT;
    if (input[1] != '.' && input[1] != ' ') state |= CONTROLLER_LEFT;
    if (input[2] != '.' && input[2] != ' ') state |= CONTROLLER_DOWN;
    if (input[3] != '.' && input[3] != ' ') state |= CONTROLLER_UP;
    if (input[4] != '.' && input[4] != ' ') state |= CONTROLLER_START;
    if (input[5] != '.' && input[5] != ' ') state |= CONTROLLER_SELECT;
    if (input[6] != '.' && input[6] != ' ') state |= CONTROLLER_B;
    if (input[7] != '.' && input[7] != ' ') state |= CONTROLLER_A;
    
    return state;
}

// Get the next controller input
uint8_t fm2_next_input(FM2Parser* parser) {
    // Skip header if we haven't done so yet
    if (!parser->header_parsed) {
        skip_header(parser);
    }
    
    // Read next line
    if (!fgets(parser->line_buffer, MAX_LINE_LENGTH, parser->file)) {
        return 0; // End of file or error
    }
    
    char* line = parser->line_buffer;

    printf("%s", line);

    if (line[0] != '|') {
        return 0; // Invalid format
    }
    
    // Find the second pipe which starts controller 1 input
    char* controller_input = strchr(line + 1, '|');
    if (!controller_input) {
        return 0; // Invalid format
    }
    
    // Skip past the pipe character to get to the actual input
    controller_input++; // Move pointer to first character of controller input
    
    // Parse the 8 characters of controller input
    return parse_controller_input(controller_input);
}

// Clean up the parser
void fm2_close(FM2Parser* parser) {
    if (parser && parser->file) {
        fclose(parser->file);
        parser->file = NULL;
    }
}

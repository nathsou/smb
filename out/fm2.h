#ifndef SMB_FM2_H
#define SMB_FM2_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Controller input bit definitions */
#define CONTROLLER_RIGHT  0b10000000
#define CONTROLLER_LEFT   0b01000000
#define CONTROLLER_DOWN   0b00100000
#define CONTROLLER_UP     0b00010000
#define CONTROLLER_START  0b00001000
#define CONTROLLER_SELECT 0b00000100
#define CONTROLLER_B      0b00000010
#define CONTROLLER_A      0b00000001

#define MAX_LINE_LENGTH 64

/* Parser state structure */
typedef struct {
    FILE* file;
    bool header_parsed;
    char line_buffer[MAX_LINE_LENGTH];
} FM2Parser;

/**
 * Initialize an FM2 parser with the specified input file
 * 
 * @param parser Pointer to an FM2Parser structure to initialize
 * @param filename Path to the FM2 file to parse
 * @return true if initialization succeeded, false otherwise
 */
bool fm2_init(FM2Parser* parser, const char* filename);

/**
 * Read the next controller input from the FM2 file
 * 
 * @param parser Pointer to an initialized FM2Parser
 * @return uint8_t containing the controller state bits, or 0 on EOF/error
 *         Bit 7: Right
 *         Bit 6: Left
 *         Bit 5: Down
 *         Bit 4: Up
 *         Bit 3: Start
 *         Bit 2: Select
 *         Bit 1: B
 *         Bit 0: A
 */
uint8_t fm2_next_input(FM2Parser* parser);

/**
 * Clean up and close the FM2 parser
 * 
 * @param parser Pointer to the FM2Parser to clean up
 */
void fm2_close(FM2Parser* parser);

#endif

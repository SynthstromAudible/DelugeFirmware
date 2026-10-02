#include <cstdint>
uint32_t __sdram_bss_end = 0;
uint32_t __frunk_bss_end = 0;
uint32_t __frunk_slack_end = 0;
uint32_t program_stack_start = 0;
uint32_t program_stack_end = 0;
uint32_t __heap_start = 0;
uint32_t currentUIMode = 0;

#define NUM_ENCODERS 6
#define NUM_FUNCTION_ENCODERS 4
#define ENCODER_SCROLL_X 1
#define ENCODER_SCROLL_Y 0
#define ENCODER_TEMPO 3
#define ENCODER_SELECT 2
#define ENCODER_MOD_0 5
#define ENCODER_MOD_1 4

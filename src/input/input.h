/****************************************************************************
*   NeoCDRX
*   NeoGeo CD Emulator
*   NeoCD Redux - Copyright (C) 2007 softdev
****************************************************************************/

#ifndef __NEOINPUT__
#define __NEOINPUT__

#define INPUT_DEV_GC        0
#define INPUT_DEV_WIIMOTE   1
#define INPUT_DEV_NUNCHUK   2
#define INPUT_DEV_CLASSIC   3
#define INPUT_DEV_USB_XINPUT 4
#define INPUT_DEV_COUNT     5

#define INPUT_MAP_A         0
#define INPUT_MAP_B         1
#define INPUT_MAP_C         2
#define INPUT_MAP_D         3
#define INPUT_MAP_START     4
#define INPUT_MAP_SELECT    5
#define INPUT_MAP_MENU      6
#define INPUT_MAP_SAVE      7
#define INPUT_MAP_COUNT     8

void update_input (void);
unsigned char read_player1 (void);
unsigned char read_player2 (void);
unsigned char read_pl12_startsel (void);
extern int accept_input;
extern u16 getMenuButtons(void);
int input_menu_button_down (void);
void input_arm_menu_release_latch (void);

/*** Runtime controller remapping ***/
const char *input_device_name (int device);
const char *input_mapping_name (int device, int action);
void input_cycle_mapping (int device, int action, int direction);
int input_capture_mapping (int device, int action);
void input_reset_mapping (int device);
void input_reset_all_mappings (void);

/*** Mapping persistence helpers ***/
int input_get_mapping_index (int device, int action);
int input_set_mapping_index (int device, int action, int index);
int input_mapping_choice_count (int device);

#endif

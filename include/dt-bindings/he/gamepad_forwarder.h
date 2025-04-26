// joystick left
#define GP_L_LEFT 0
#define GP_L_RIGHT 1
#define GP_L_UP 2
#define GP_L_DOWN 3                                         
// joystick right
#define GP_R_LEFT 4
#define GP_R_RIGHT 5
#define GP_R_UP 6
#define GP_R_DOWN 7
// left trigger
#define GP_L_TR 9
// right trigger
#define GP_R_TR 11
#define GP_CODE_COUNT 12

#define GP_CODE_TO_IDX(code) (int)(code / 2)
#define GP_IS_JOYSTICK(code) (code >= 0 && code <= GP_R_DOWN)
#define GP_IS_TRIGGER(code) (code == GP_L_TR || code == GP_R_TR)
#define GP_IS_LEFT(code) (code >= 0 && (code <= GP_L_DOWN || code == GP_L_TR))
#define GP_IS_RIGHT(code)                                                      \
    (code >= GP_R_LEFT && (code <= GP_R_DOWN || code == GP_R_TR))
#define GP_INVALID(code)                                                       \
    (code < 0 || code >= GP_CODE_COUNT || code == 8 || code == 10)

#define GP_BTN_COUNT 8

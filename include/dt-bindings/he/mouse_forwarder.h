#define MOUSE_X_LEFT 0
#define MOUSE_X_RIGHT 1
#define MOUSE_Y_UP 2
#define MOUSE_Y_DOWN 3

#define MOUSE_H_WHEEL_LEFT 4
#define MOUSE_H_WHEEL_RIGHT 5
#define MOUSE_V_WHEEL_UP 6
#define MOUSE_V_WHEEL_DOWN 7
#define MOUSE_CODE_COUNT 8

#define MOUSE_CODE_TO_IDX(code) (int)(code / 2)
#define MOUSE_IS_MOVEMENT(code) (code >= 0 && code <= MOUSE_Y_DOWN)
#define MOUSE_IS_WHEEL(code) (code >=MOUSE_H_WHEEL_LEFT && code <= MOUSE_V_WHEEL_DOWN)

#define MOUSE_INVALID(code)                                                       \
    (code < 0 || code >= MOUSE_CODE_COUNT)


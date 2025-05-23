# ZMK Hall Effect feature module
This module is a collection of drivers, behaviors and input-processors to support Hall-effect switches and features commonly found on HE keyboards
# Features
- **Adjustable actuation**
- **Rapid Trigger**
- **SOCD**
- **Dynamic keystroke**
- **Mouse input**
- **Gamepad input**
## Drivers
  - `he,kscan-he-direct-pulsed`: Each HE sensor is wired directly to an adc channel, supports the use of an enable pin to power the sensor only when it needs to be read. <br/>
    Generates input events with type `INPUT_EV_HE` but can also generate kscan events with `he,kscan-he-direct-pulsed-forwarder`(this allows the input processors to use the normal keymap)
> This was designed to work with my custom HE keyboard, if you need another driver(e.g. using analog multiplexers), you can open an issue (note that i have no way to test other drivers at the moment)

  - `zmk,battery-nrf-vddh-channel`: Has the same functionality as `zmk,battery-nrf-vddh` but you can select which adc channel gets used
> [!NOTE]
> Currently if you enable both drivers using non-overlapping channels (which is necessary to enable both), everything stops working.
> So this driver is pretty much useless for now.
> This also happens with `zmk,battery-nrf-vddh`

## Input Processors
#### These are the analog equivalent of behaviors

- `he,raw-signal-processor`: Apply a SOS filter to the input signal, this is used to reduce the noise of the sensor
- `he,input-processor-matrix-offset`: Add an offset in position to the received inputs (the value of the input remains the same, only moves the matrix position)
- `he,input-processor-keymap`: The analog equivalent of `zmk,keymap`, passes a received value to the processor for the corresponding key, uses input-processors instead of behaviors
- `he,input-processor-adjustable-actuation`: Press a key(triggers the behavior in `zmk,keymap`) or some explicit behaviors when the switch passes a certain height, supports multiple actuation points and unidirectional triggers
- `he,input-processor-rapid-trigger`: Rapid Trigger(press or release a key when the switch changes direction), only supports continuous mode
- `he,input-processor-socd`: SOCD (Simultaneous Opposing Cardinal Directions), supports 4 priority modes: `last key`, `first key`, `deeper key` or `direction`(a combination of last key and rapid trigger)
- `he,input-processor-blank-he`: The analog equivalent of `&trans` and `&none` behaviors, can log when it receives a value
- `he,input-processor-gamepad-forwarder`: Send a joystick or analog trigger event proportional to the switch height, emulating a gamepad
- `he,input-processor-mouse-forwarder`: Send X,Y, vertical or horizontal wheel event proportional to the switch height


## Behaviors
- `he,behavior-gamepad-btn`: Send a gamepad button event
- `he,behavior-set-pulse`: Set the pulse mode of `he,kscan-he-direct-pulsed` (turn off the enable pin after the read is done / keep the enable pin always on)

## Other
- `he,input-listener`: Same as `zmk,input-listener` but it executes the input-processors of the parent before going to the ones in the layers and stops when receiving any `ZMK_INPUT_PROC_STOP`
- `he,input-split`: Same as `zmk,input-split` but doesn't send the input when it receives a `ZMK_INPUT_PROC_STOP` (only for the peripheral side)
- `he,kscan-he-direct-pulsed-forwarder`: Allows for external control of `he,kscan-he-direct-pulsed`(generate kscan event outside of the driver and control how the driver works)

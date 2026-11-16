//
// PICO private extension header: XR_PICO_dynamic_object_pico_keyboard
// Mirrors the definitions provided by the PICO OpenXR runtime headers
// (see D:/SDK/OpenXrHeaders/openxr_pico/private/spatial_sense_3_0/pico_dynamic_object_pico_keyboard.h).
//

#ifndef PICOXRRUNTIME_PICO_DYNAMIC_OBJECT_PICO_KEYBOARD_H
#define PICOXRRUNTIME_PICO_DYNAMIC_OBJECT_PICO_KEYBOARD_H

#include "openxr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XR_PICO_dynamic_object_pico_keyboard 1
#define XR_PICO_dynamic_object_pico_keyboard_SPEC_VERSION 1
#define XR_PICO_DYNAMIC_OBJECT_PICO_KEYBOARD_EXTENSION_NAME "XR_PICO_dynamic_object_pico_keyboard"

static const XrDynamicObjectTypePICO XR_DYNAMIC_OBJECT_TYPE_PICO_KEYBOARD_PICO = (XrDynamicObjectTypePICO)1010022001;
static const XrDynamicObjectTypePICO XR_DYNAMIC_OBJECT_TYPE_PICO_TOUCHPAD_PICO = (XrDynamicObjectTypePICO)1010022002;

static const XrPeripheralCategoryPICO XR_PERIPHERAL_CATEGORY_UNCATEGORIZED_TOUCHPAD_PICO = (XrPeripheralCategoryPICO)1010022001;

static const XrStructureType XR_TYPE_SYSTEM_DYNAMIC_OBJECT_PICO_KEYBOARD_PROPERTIES_PICO = (XrStructureType)1010022001;
typedef struct XrSystemDynamicObjectPicoKeyboardPropertiesPICO {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    XrBool32                    supportsDynamicObjectPicoKeyboard;
} XrSystemDynamicObjectPicoKeyboardPropertiesPICO;

typedef XrFlags64 XrPicoKeyboardPropertyPICO;
static const XrPicoKeyboardPropertyPICO XR_PICO_KEYBOARD_PROPERTY_AVAILABLE_STATE_BIT = 0x00000001;
static const XrPicoKeyboardPropertyPICO XR_PICO_KEYBOARD_PROPERTY_NOT_AVAILABLE_STATE_BIT = 0x00000002;

static const XrStructureType XR_TYPE_PERIPHERAL_PICO_KEYBOARD_DATA_PICO = (XrStructureType)1010022002;
typedef struct XrPeripheralPicoKeyboardDataPICO {
    XrStructureType                     type;
    void*                               next;
    float                               scale;
    XrPicoKeyboardPropertyPICO          properties;
} XrPeripheralPicoKeyboardDataPICO;

#ifdef __cplusplus
}
#endif

#endif //PICOXRRUNTIME_PICO_DYNAMIC_OBJECT_PICO_KEYBOARD_H

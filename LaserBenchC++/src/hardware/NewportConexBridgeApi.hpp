#pragma once

#if defined(NEWPORT_CONEX_BRIDGE_EXPORTS)
#define LB_NEWPORT_API __declspec(dllexport)
#else
#define LB_NEWPORT_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct lb_newport_axis_handle;

LB_NEWPORT_API int lb_newport_scan_devices(
    char* out_devices,
    int out_devices_size,
    char* out_error,
    int out_error_size
);

LB_NEWPORT_API int lb_newport_axis_create(
    const char* port,
    int address,
    lb_newport_axis_handle** out_handle,
    char* out_error,
    int out_error_size
);

LB_NEWPORT_API int lb_newport_axis_destroy(lb_newport_axis_handle* handle);

LB_NEWPORT_API int lb_newport_axis_open(
    lb_newport_axis_handle* handle,
    char* out_error,
    int out_error_size
);

LB_NEWPORT_API int lb_newport_axis_close(
    lb_newport_axis_handle* handle,
    char* out_error,
    int out_error_size
);

LB_NEWPORT_API int lb_newport_axis_stop(
    lb_newport_axis_handle* handle,
    char* out_error,
    int out_error_size
);

LB_NEWPORT_API int lb_newport_axis_home(
    lb_newport_axis_handle* handle,
    int timeout_ms,
    char* out_state_code,
    int out_state_code_size,
    char* out_error,
    int out_error_size
);

LB_NEWPORT_API int lb_newport_axis_move_absolute(
    lb_newport_axis_handle* handle,
    double position_mm,
    int timeout_ms,
    char* out_error,
    int out_error_size
);

LB_NEWPORT_API int lb_newport_axis_move_relative(
    lb_newport_axis_handle* handle,
    double delta_mm,
    int timeout_ms,
    char* out_error,
    int out_error_size
);

LB_NEWPORT_API int lb_newport_axis_read_state(
    lb_newport_axis_handle* handle,
    char* out_error_code,
    int out_error_code_size,
    char* out_state_code,
    int out_state_code_size,
    char* out_error,
    int out_error_size
);

LB_NEWPORT_API int lb_newport_axis_read_position(
    lb_newport_axis_handle* handle,
    double* out_position_mm,
    char* out_error,
    int out_error_size
);

#ifdef __cplusplus
}
#endif

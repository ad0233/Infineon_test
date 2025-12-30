set(lib_srcs        
        "source/ifx_flip_f32.c"
        "source/ifx_rotate_f32.c"
        "source/ifx_peak_search_f32.c"
        "source/ifx_arcsin_f32.c"
        "source/ifx_angle_monopulse_f32.c"
        "source/ifx_angle_dbf_f32.c"
        "source/ifx_gen_steering_matrix_f32.c"
        "source/ifx_mti_f32.c"

        "source/ifx_mean_removal_f32.c"
        "source/ifx_cmplx_mean_removal_f32.c"

        "source/ifx_window_blackman_f32.c"
        "source/ifx_window_blackmanharris_f32.c"
        "source/ifx_window_hamming_f32.c"
        "source/ifx_window_hann_f32.c"

        "source/ifx_range_fft_f32.c"
        "source/ifx_range_cfft_f32.c"
        "source/ifx_doppler_cfft_f32.c"
        "source/ifx_shift_cfft_f32.c"
)

idf_component_register(SRCS "${lib_srcs}"
                       INCLUDE_DIRS "include"
                       REQUIRES "espressif__esp-dsp")
# EmberWarningOptions
#
# Central compiler warning configuration. Every engine target links this module
# so that warning behaviour is identical across the whole codebase.

function(ember_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /wd4100  # unreferenced formal parameter (common in interface stubs)
        )
        if(EMBER_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
            -Wnull-dereference
            -Wdouble-promotion
        )
        if(EMBER_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# Bir hedef için katı derleyici uyarılarını açar.
# INSPECTOR_WARNINGS_AS_ERRORS=ON ise (CI'da açık) her uyarı derlemeyi durdurur:
# uyarılar "sonra bakarız" diye birikmez.
function(inspector_set_warnings target)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor>
        $<$<AND:$<BOOL:${INSPECTOR_WARNINGS_AS_ERRORS}>,$<CXX_COMPILER_ID:GNU,Clang>>:-Werror>)
endfunction()

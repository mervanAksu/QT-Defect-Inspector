# Bir hedef için katı derleyici uyarılarını açar.
function(inspector_set_warnings target)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor>)
endfunction()

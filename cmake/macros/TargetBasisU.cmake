# 
#  Copyright 2026 Overte e.V.
#
#  Distributed under the Apache License, Version 2.0.
#  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
# 
macro(TARGET_BASISU)
    find_package(basisu REQUIRED)
    target_link_libraries(${TARGET_NAME} basisu::basisu_encoder)
endmacro()

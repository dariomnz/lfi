
/*
 *  Copyright 2024-2025 Dario Muñoz Muñoz, Felix Garcia Carballeira, Diego Camarmas Alonso, Alejandro Calderon Mateos
 *
 *  This file is part of LFI.
 *
 *  LFI is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  LFI is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with LFI.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include <iostream>

namespace LFI
{
    constexpr const char *file_name(const char *path)
    {
        const char *file = path;
        while (*path)
        {
            if (*path++ == '/')
            {
                file = path;
            }
        }
        return file;
    }

#ifdef DEBUG
    #define debug_error(out_format) std::cerr << "[ERROR] [" << __func__ << "] [" << ::LFI::file_name(__FILE__) << ":" << __LINE__ << "] " << out_format << std::endl;
    #define debug_warning(out_format) std::cerr << "[WARNING] [" << __func__ << "] [" << ::LFI::file_name(__FILE__) << ":" << __LINE__ << "] " << out_format << std::endl;
    #define debug_info(out_format) std::cerr << "[INFO] [" << __func__ << "] [" << ::LFI::file_name(__FILE__) << ":" << __LINE__ << "] " << out_format << std::endl;
#else
    #define debug_error(out_format)
    #define debug_warning(out_format)
    #define debug_info(out_format)
#endif

#define print(out_format) std::cout << out_format << std::endl;

} // namespace LFI
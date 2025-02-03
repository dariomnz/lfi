
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

#include <dlfcn.h>
#include <string>
#include <stdexcept>
#include <debug.hpp>

#define PROXY(func) \
    ::lookupSymbol<::func>(#func)

static auto getSymbol(const char *name)
{
    auto symbol = ::dlsym(RTLD_NEXT, name);
    if (!symbol)
    {
        std::string errormsg = "dlsym failed to find symbol '";
        errormsg += name;
        errormsg += "'";
        throw std::runtime_error(errormsg);
    }
    return symbol;
}

template <auto T>
static auto lookupSymbol(const char *name)
{
    using return_type = decltype(T);
    static return_type symbol = (return_type)getSymbol(name);
    return symbol;
}
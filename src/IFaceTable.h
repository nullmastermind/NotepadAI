/*
 * This file is part of Notepad Next.
 * Copyright 2019 Justin Dailey
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */


#ifndef IFACETABLE_H
#define IFACETABLE_H

#include <cstdint>
#include <span>
#include <string>
#include <vector>

enum IFaceType : std::uint8_t {
    iface_void,
    iface_int,
    iface_length,
    iface_position,
    iface_colour,
    iface_bool,
    iface_keymod,
    iface_string,
    iface_stringresult,
    iface_cells, // This and everything else below is not "scriptable"
    iface_textrange,
    iface_findtext,
    iface_formatrange
};

struct IFaceConstant {
    const char *name;
    int value;
};

struct IFaceFunction {
    const char *name;
    int value;
    IFaceType returnType;
    IFaceType paramType[2];
};

struct IFaceProperty {
    const char *name;
    int getter;
    int setter;
    IFaceType valueType;
    IFaceType paramType;

    IFaceFunction GetterFunction() const {
        IFaceFunction result = {"(property getter)",getter,valueType,{paramType,iface_void}};
        return result;
    }

    IFaceFunction SetterFunction() const {
        IFaceFunction result = {"(property setter)",setter,iface_void,{valueType, iface_void}};
        if (paramType != iface_void) {
            result.paramType[0] = paramType;
            if (valueType == iface_stringresult)
                result.paramType[1] = iface_string;
            else
                result.paramType[1] = valueType;
        }
        if ((paramType == iface_void) && ((valueType == iface_string) || (valueType == iface_stringresult))) {
            result.paramType[0] = paramType;
            if (valueType == iface_stringresult)
                result.paramType[1] = iface_string;
            else
                result.paramType[1] = valueType;
        }

        return result;
    }
};

inline bool IFaceTypeIsScriptable(IFaceType t, int index) {
    return t < iface_stringresult || (index == 1 && t == iface_stringresult);
}

inline bool IFaceTypeIsNumeric(IFaceType t) {
    return (t > iface_void && t < iface_bool);
}

inline bool IFaceFunctionIsScriptable(const IFaceFunction &f) {
    return IFaceTypeIsScriptable(f.paramType[0], 0) && IFaceTypeIsScriptable(f.paramType[1], 1);
}

inline bool IFacePropertyIsScriptable(const IFaceProperty &p) {
    return (((p.valueType > iface_void) && (p.valueType <= iface_stringresult) && (p.valueType != iface_keymod)) &&
        ((p.paramType < iface_colour) || (p.paramType == iface_string) || (p.paramType == iface_bool)) && (p.getter || p.setter));
}

class IFaceTableInterface {
public:
    virtual const IFaceConstant *FindConstant(const char *name) const = 0;
    virtual const IFaceFunction *FindFunction(const char *name) const = 0;
    virtual const IFaceFunction *FindFunctionByConstantName(const char *name) const = 0;
    virtual const IFaceFunction *FindFunctionByValue(int value) const = 0;
    virtual const IFaceProperty *FindProperty(const char *name) const = 0;
    virtual int GetConstantName(int value, char *nameOut, unsigned nameBufferLen, const char *hint) const = 0;
    virtual const IFaceFunction *GetFunctionByMessage(int message) const = 0;
    virtual IFaceFunction GetPropertyFuncByMessage(int message) const = 0;
};

class IFaceTable : public IFaceTableInterface {
public:
    IFaceTable(const char *_prefix,
        std::span<const IFaceFunction> _functions,
        std::span<const IFaceConstant> _constants,
        std::span<const IFaceProperty> _properties) noexcept :
        prefix(_prefix),
        functions(_functions),
        constants(_constants),
        properties(_properties)
    {}

    const char *prefix;

    std::span<const IFaceFunction> functions;
    std::span<const IFaceConstant> constants;
    std::span<const IFaceProperty> properties;

    // IFaceTableInterface
    const IFaceConstant *FindConstant(const char *name) const override;
    const IFaceFunction *FindFunction(const char *name) const override;
    const IFaceFunction *FindFunctionByConstantName(const char *name) const override;
    const IFaceFunction *FindFunctionByValue(int value) const override;
    const IFaceProperty *FindProperty(const char *name) const override;
    int GetConstantName(int value, char *nameOut, unsigned nameBufferLen, const char *hint) const override;
    const IFaceFunction *GetFunctionByMessage(int message) const override;
    IFaceFunction GetPropertyFuncByMessage(int message) const override;

    std::vector<std::string> GetAllConstantNames() const;
    std::vector<std::string> GetAllFunctionNames() const;
    std::vector<std::string> GetAllPropertyNames() const;
};

#endif

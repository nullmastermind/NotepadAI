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


#ifndef IFACETABLEMIXER_H
#define IFACETABLEMIXER_H

#include "IFaceTable.h"
#include <vector>

class IFaceTableMixer : public IFaceTableInterface {
private:
	std::vector<IFaceTableInterface *> ifaces;

public:
	void AddIFaceTable(IFaceTableInterface *iface) { ifaces.push_back(iface); }

	// IFaceTableInterface
	const IFaceConstant *FindConstant(const char *name) const override;
	const IFaceFunction *FindFunction(const char *name) const override;
	const IFaceFunction *FindFunctionByConstantName(const char *name) const override;
	const IFaceFunction *FindFunctionByValue(int value) const override;
	const IFaceProperty *FindProperty(const char *name) const override;
	int GetConstantName(int value, char *nameOut, unsigned nameBufferLen, const char *hint) const override;
	const IFaceFunction *GetFunctionByMessage(int message) const override;
	IFaceFunction GetPropertyFuncByMessage(int message) const override;
};

#endif

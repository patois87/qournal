/*
 * Qournal
 *
 * Values between Lua and Qt, for the functions the plugins call
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QMap>
#include <QMetaType>
#include <QVariant>

struct lua_State;

/// A Lua table with integer keys that do not start at 1 or have gaps
using LuaIntTable = QMap<qlonglong, QVariant>;
Q_DECLARE_METATYPE(LuaIntTable)

namespace LuaBridge {

/**
 * The Lua value at an index of the stack. Tables with the keys 1..n become a QVariantList, other tables a
 * QVariantMap (integer keys as text). Strings become a QString, or a QByteArray if they are not text (image data).
 * Light userdata becomes a void*; functions and other values are left out.
 */
QVariant toVariant(lua_State* L, int index);

/// Pushes a value: lists as tables with the keys 1..n, maps and LuaIntTable as tables, colours as 0xRRGGBB
void push(lua_State* L, const QVariant& value);

}  // namespace LuaBridge

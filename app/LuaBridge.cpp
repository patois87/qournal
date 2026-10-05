#include "LuaBridge.h"

#include <QColor>
#include <QStringDecoder>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace {

constexpr int MAX_DEPTH = 32;  // tables can contain themselves

QVariant valueToVariant(lua_State* L, int index, int depth);

QVariant tableToVariant(lua_State* L, int index, int depth) {
    index = lua_absindex(L, index);
    // Keys and values of every level of nested tables take room on the stack of Lua
    if (depth >= MAX_DEPTH || !lua_checkstack(L, 4)) {
        return {};
    }
    // A sequence, if the keys are exactly 1..n
    const lua_Integer length = static_cast<lua_Integer>(lua_rawlen(L, index));
    lua_Integer count = 0;
    bool sequence = true;
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        ++count;
        if (!lua_isinteger(L, -2) || lua_tointeger(L, -2) < 1 || lua_tointeger(L, -2) > length) {
            sequence = false;
        }
        lua_pop(L, 1);
    }
    if (sequence && count == length && count > 0) {
        QVariantList list;
        for (lua_Integer i = 1; i <= length; ++i) {
            lua_rawgeti(L, index, i);
            list.append(valueToVariant(L, -1, depth + 1));
            lua_pop(L, 1);
        }
        return list;
    }

    QVariantMap map;
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        QString key;
        if (lua_isinteger(L, -2)) {
            key = QString::number(lua_tointeger(L, -2));
        } else if (lua_type(L, -2) == LUA_TSTRING) {
            key = QString::fromUtf8(lua_tostring(L, -2));
        }
        if (!key.isNull()) {
            map.insert(key, valueToVariant(L, -1, depth + 1));
        }
        lua_pop(L, 1);
    }
    return map;
}

QVariant valueToVariant(lua_State* L, int index, int depth) {
    switch (lua_type(L, index)) {
        case LUA_TBOOLEAN:
            return lua_toboolean(L, index) != 0;
        case LUA_TNUMBER:
            if (lua_isinteger(L, index)) {
                return static_cast<qlonglong>(lua_tointeger(L, index));
            }
            return static_cast<double>(lua_tonumber(L, index));
        case LUA_TSTRING: {
            size_t length = 0;
            const char* data = lua_tolstring(L, index, &length);
            const QByteArray bytes(data, static_cast<qsizetype>(length));
            QStringDecoder decoder(QStringDecoder::Utf8);
            const QString text = decoder.decode(bytes);
            if (decoder.hasError() || bytes.contains('\0')) {
                return bytes;
            }
            return text;
        }
        case LUA_TTABLE:
            return tableToVariant(L, index, depth);
        case LUA_TLIGHTUSERDATA:
            return QVariant::fromValue(lua_touserdata(L, index));
        default:
            return {};
    }
}

}  // namespace

QVariant LuaBridge::toVariant(lua_State* L, int index) { return valueToVariant(L, index, 0); }

void LuaBridge::push(lua_State* L, const QVariant& value) {
    // Room for the value and, if it is a table, for one of its entries
    luaL_checkstack(L, 3, "value too deeply nested");
    if (value.userType() == qMetaTypeId<LuaIntTable>()) {
        const auto table = value.value<LuaIntTable>();
        lua_createtable(L, static_cast<int>(table.size()), 0);
        for (auto it = table.begin(); it != table.end(); ++it) {
            push(L, it.value());
            lua_rawseti(L, -2, static_cast<lua_Integer>(it.key()));
        }
        return;
    }
    switch (value.typeId()) {
        case QMetaType::UnknownType:
        case QMetaType::Nullptr:
            lua_pushnil(L);
            break;
        case QMetaType::Bool:
            lua_pushboolean(L, value.toBool() ? 1 : 0);
            break;
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::Long:
        case QMetaType::ULong:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
        case QMetaType::Short:
        case QMetaType::UShort:
            lua_pushinteger(L, static_cast<lua_Integer>(value.toLongLong()));
            break;
        case QMetaType::Double:
        case QMetaType::Float:
            lua_pushnumber(L, static_cast<lua_Number>(value.toDouble()));
            break;
        case QMetaType::QByteArray: {
            const QByteArray bytes = value.toByteArray();
            lua_pushlstring(L, bytes.constData(), static_cast<size_t>(bytes.size()));
            break;
        }
        case QMetaType::QColor:
            lua_pushinteger(L, static_cast<lua_Integer>(value.value<QColor>().rgb() & 0xffffffU));
            break;
        case QMetaType::VoidStar:
            lua_pushlightuserdata(L, value.value<void*>());
            break;
        case QMetaType::QStringList:
        case QMetaType::QVariantList: {
            const QVariantList list = value.toList();
            lua_createtable(L, static_cast<int>(list.size()), 0);
            for (qsizetype i = 0; i < list.size(); ++i) {
                push(L, list[i]);
                lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
            }
            break;
        }
        case QMetaType::QVariantMap: {
            const QVariantMap map = value.toMap();
            lua_createtable(L, 0, static_cast<int>(map.size()));
            for (auto it = map.begin(); it != map.end(); ++it) {
                push(L, it.value());
                lua_setfield(L, -2, it.key().toUtf8().constData());
            }
            break;
        }
        default: {
            const QByteArray text = value.toString().toUtf8();
            lua_pushlstring(L, text.constData(), static_cast<size_t>(text.size()));
            break;
        }
    }
}

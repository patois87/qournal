# Plugins

The plugins of Xournal++ (the `plugins` folder of its repository), unchanged, licensed under the GNU GPL v2 or
later like Xournal++ itself. `luapi_application.def.lua` documents the functions a plugin can call; the same
functions are available here, see `app/PluginApi.cpp`.

A plugin is a folder with a `plugin.ini` and a Lua file. Plugins are looked for here (next to the program, in its
`plugins` folder) and in the `plugins` folder of the data directory of the user. They are switched on and off in
the plugin manager of the application.

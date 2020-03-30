# trace_api_plugin

## Building the plugin [Install on your nodeos server]
```
#cd eos/plugins/
#git clone this repo

edit eos/plugins/CMakeLists.txt:
#add_subdirectory(trace_api_plugin)

edit eos/programs/nodeos/CMakeLists.txt:
#target_link_libraries( nodeos PRIVATE -Wl,${whole_archive_flag} trace_api_plugin -Wl,${no_whole_archive_flag} )
```
## How to setup on your nodeos
Enable this plugin using --plugin option to nodeos or in your config.ini. Use nodeos --help to see options used by this plugin.


## Configuration
`trace-rpc-abi` `trace-no-abis` are disabled compared with original implementation.  
This plugin is designed to persist actions traces from all accounts.

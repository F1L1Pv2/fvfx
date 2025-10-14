# FVFX - Simple video editor

# How to BUILD?

--------------

### LINUX

- Install following stuff
```sh
    sudo apt install libx11-dev libxrandr-dev libvulkan-dev libshaderc-dev vulkan-validationlayers vulkan-tools
```
- Compile it like this
```sh
    clang -o nob nob.c
    ./nob release run
```

### WINDOWS

- Download VulkanSDK using this site https://vulkan.lunarg.com/
- Download clang (for clang you will also need msvc not sure why) (or use mingw)
- Compile it like this
```sh
    clang -o nob.exe nob.c
    ./nob release run
```
> [!NOTE] 
> if you istalled VulkanSDK in a folder different than `C:/VulkanSDK` specify your folder at the top of `nob.c` `VULKAN_SDK_SEARCH_PATH` macro

--------------

## Things of note

> [!NOTE] 
> If you dont specify `release` in during running nob it will build with debug information and `run` option will run app after successfull compilation

> [!NOTE]
> You only need to compile nob.c once

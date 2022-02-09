# D++ Eval Bot

This is a C++ code evaluation bot for experimenting with the [D++ library](https://dpp.dev). Please note that this bot is designed to be used only by the person who hosts the bot as it provides full unrestricted access to the server it runs on via compiled code within the bot's memory space.

This may be considered by some to be a vulnerability. In the right hands of a responsible developer this is a fantastic tool for experimenting with the Discord API and D++ library.

## Compilation

Configure your user ID in the file `evalbot/eval.h` by changing the value of `MY_DEVELOPER`. Note this can only be changed by a recompile and restart. This is by design as a security measure.

    mkdir build
    cd build
    cmake ..
    make -j

If DPP is installed in a different location you can specify the root directory to look in while running cmake 

    cmake .. -DDPP_ROOT_DIR=<your-path>

## Running the bot

Create a config.json in the directory above the build directory:

```json
{
"token": "your bot token here", 
"homeserver": "server id of server where the bot should run"
}
```

You should then make a folder called `tmp` within the build directory which will contain cached shared objects of compiled code from /eval commands.

Start the bot:

    cd build
    ./evalbot

Using the bot:

Type `/eval` and enter code into the modal dialog box when prompted!
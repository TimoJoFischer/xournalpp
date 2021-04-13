--[[
Why this plugin ?
    With a huge display and a small wacom tablet you need always scroll into the document to write.
    To avoid this problem, this plugin can help.

!!! Following dependencies:
    - linux
    - X11
    - wacom drivers (https://linuxwacom.github.io/)
    - cairo (https://www.cairographics.org/)
    - xorg-xinput

 To work properly:
    1. get your device x_input and adapt it in code
    2. adjust border_width/border_height
    3. adjust default_width/default_height

General functionality:
    - map wacom drawing tablet only to part of the screen
    - the mapped destination on the display is marked by a rectangle
    - the mapped destination can be changed with Ctrl+(W/A/S/D)
    - Ctrl+R map the tablet to fullscreen

Future work should be (help would be nice):
    - better implementation
    - auto detect tablet
    - more supports (other tablets/distribitions)
]]

function initUi()
    -- border_height and border_width should have the same ratio as your drawing tablet
    border_height = 350 -- TODO: adjust this to your setup
    border_width = 560 -- TODO: adjust this to your setup
    border_linewidth = 5
    app.registerUi({["menu"] = "Move Right", ["callback"] = "moveRight", ["accelerator"]="<Alt>d"});
    app.registerUi({["menu"] = "Move Left", ["callback"] = "moveLeft", ["accelerator"] = "<Alt>a"});
    app.registerUi({["menu"] = "Move Up", ["callback"] = "moveUp", ["accelerator"] = "<Alt>w"});
    app.registerUi({["menu"] = "Move Down", ["callback"] = "moveDown", ["accelerator"] = "<Alt>s"});
    app.registerUi({["menu"] = "Default mapping", ["callback"] = "half", ["accelerator"] = "<Alt>r"});

    posX = 50;
    posY = 200;

    -- get xinput id of your wacom device (name need to be changed)
    id_tablet = os.capture("xinput list --id-only 'Wacom One by Wacom S Pen stylus'");
end

function moveRecangle()
    os.execute("pkill -f createBorder");
    os.execute("xsetwacom set "..id_tablet.." Area 0 0 15200 9500");
    os.execute("xsetwacom set "..id_tablet.." MapToOutput "..border_width.."x"..border_height.."+".. tostring(posX) .. "+" .. tostring(posY));
    -- TODO: adjust location of the executable c++ program
    os.execute("/home/timo/Programme/xournalpp/plugins/Wacom_ScreenArea/createBorder " .. tostring(posX) .. " " .. tostring(posY) .. " " .. tostring(border_width) .. " " .. tostring(border_height) .. " " .. tostring(border_linewidth) .. "&");
end

function moveRight()
    posX=posX+50;
    moveRecangle()
end

function moveLeft()
    posX=posX-50;
    moveRecangle()
end

function moveUp()
    posY=posY-50;
    moveRecangle()
end

function moveDown()
    posY=posY+50;
    moveRecangle()
end

function half()
    os.execute("pkill -f createBorder");
    -- TODO: adjust the values to your setup ( i map the tablet to half of my screen)
    os.execute("xsetwacom set "..id_tablet.." Area 0 0 11347 9500");
    os.execute("xsetwacom set "..id_tablet.." MapToOutput 1720x1440+0+0");
end

function os.capture(cmd)
    local handle = assert(io.popen(cmd, 'r'))
    local output = assert(handle:read('*a'))
    handle:close()
    return tonumber(output)
end
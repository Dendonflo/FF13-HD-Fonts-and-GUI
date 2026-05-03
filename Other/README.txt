================================================================================
                    FF XIII HD FONT & GUI MOD
================================================================================

Replaces the original Final Fantasy XIII fonts, GUI elements, and map tiles
with high-resolution versions, resulting in sharper, cleaner visuals across
all menus, UI, and in-game maps.

--------------------------------------------------------------------------------
!! IMPORTANT -- READ BEFORE UPGRADING FROM AN OLDER 2.X VERSION !!
--------------------------------------------------------------------------------

  IF YOU ARE UPGRADING FROM A PREVIOUS VERSION 2.X OF THIS MOD (d3d9.dll), YOU MUST FIRST
  FULLY UNINSTALL THE OLD VERSION BEFORE INSTALLING THIS ONE.

  THE PREVIOUS 2.X RELEASES SHIPPED AS d3d9.dll AND REPLACED FF13FIX ENTIRELY.
  THIS VERSION SHIPS AS version.dll AND WORKS ALONGSIDE FF13FIX.

  HAVING BOTH VERSIONS INSTALLED AT THE SAME TIME WILL CAUSE CONFLICTS AND CRASHES.

  TO UNINSTALL THE OLD VERSION:
    DELETE d3d9.dll and FF13Fix.ini FROM THE GAME FOLDER, THEN REINSTALL FF13FIX OR FF13FIX PLUS NORMALLY
    BEFORE PROCEEDING WITH THE INSTALLATION BELOW.

--------------------------------------------------------------------------------
!! IMPORTANT -- FF13FIX PLUS USERS: BLURRY UI FIX !!
--------------------------------------------------------------------------------

  IF YOU ARE USING FF13FIX PLUS, SOME UI ELEMENTS (map icons, select cursor, minimap border, ...) 
  MAY APPEAR BLURRY WITH THIS MOD INSTALLED. THIS IS CAUSED BY FF13FIX PLUS'S 
  ANISOTROPIC FILTERING FIX APPLYING A LOD BIAS THAT BLURS TEXTURES THAT HAVE 
  BEEN UPSCALED BY THIS MOD. A PERMANENT FIX IS CURRENTLY BEING WORKED ON.

  TO FIX IT, OPEN FF13Fix.ini AND CHANGE THE FOLLOWING LINE:

    LODBias = 1.00000

  TO:

    LODBias = 0.0

  THIS ONLY AFFECTS FF13FIX PLUS. THE ORIGINAL FF13FIX DOES NOT HAVE THIS
  SETTING AND REQUIRES NO CHANGES.

--------------------------------------------------------------------------------
REQUIREMENTS
--------------------------------------------------------------------------------

  4GB Large Address Aware patch
    Patching the executable allows the game to use more than 2GB of RAM,
    reducing crashes especially at high resolutions or with heavy texture mods.
    Copy the unpatched ffxiiiimg.exe to the bin folder as untouched.exe,
    then patch the original: https://ntcore.com/?page_id=371

  OR

  The Nova Chrysalia Launcher will automatically patch the game for 4GB
  https://github.com/LR-Research-Team/Datalog/wiki/%5BGUIDE%5D-Setting-up-the-Nova-Chrysalia-Mod-manager

--------------------------------------------------------------------------------
INSTALLATION
--------------------------------------------------------------------------------

  Copy the following into:
       <game_install_folder>/white_data/prog/win/bin/

         - version.dll
         - hd_textures/ (entire folder)

     Overwrite if prompted.

  The mod will load automatically on next launch.
  Check HDTextures.log in the same folder to confirm it initialised correctly.

--------------------------------------------------------------------------------
UNINSTALLATION
--------------------------------------------------------------------------------

    Delete version.dll and the hd_textures/ folder.

--------------------------------------------------------------------------------
COMPATIBILITY
--------------------------------------------------------------------------------

  FF13Fix / FF13Fix PLUS
    Fully compatible. Install FF13Fix first, then this mod on top.
    If you are using the FF13Fix PLUS version, check the disclaimer at the top of this
    file to see how to fix blurry UI

  DXVK
    Fully compatible.

  ReShade
    No known conflict.

--------------------------------------------------------------------------------
TROUBLESHOOTING
--------------------------------------------------------------------------------

  Mod not loading / textures unchanged
    Check that version.dll and hd_textures/ (with hash_database.txt inside it)
    are in the same folder as ffxiiiimg.exe. Open HDTextures.log and confirm
    you see "HDTextures mod loaded" near the top and a non-zero texture count.

  Game crashes on startup
    If you are upgrading from the old d3d9.dll version of this mod, make sure
    you have fully uninstalled it first. See the warning at the top of this file.

  Game crashes after playing for a while
    See the 4GB patch requirment described earlier in this file.

  Conflict with another mod that uses version.dll
    This mod uses version.dll as its injection point. If another mod also
    occupies version.dll, one of them will need to use a different proxy DLL
    (e.g. winmm.dll, dinput8.dll). This mod's source is available if a
    recompile targeting a different DLL is needed.

--------------------------------------------------------------------------------
CREDITS
--------------------------------------------------------------------------------

  HD Font & GUI Mod    Dendonflo

--------------------------------------------------------------------------------
SPECIAL THANKS
--------------------------------------------------------------------------------

  FF13Fix -- PureDark, RaiderB and contributors
  https://github.com/rebtd7/FF13Fix

  This mod's injection and hooking architecture was originally developed as
  a fork of FF13Fix. The current standalone version has been fully rewritten
  as an independent module, but FF13Fix's work on understanding the game's
  internals and its compatibility fixes were invaluable in getting here.
  Install FF13Fix. It is still the best general-purpose fix mod for the game.

================================================================================

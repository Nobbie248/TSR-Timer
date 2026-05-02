using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

namespace TSRTimer
{
    public static class AutoSplitBridge
    {
        const string ProcessName = "TimeSplittersRewind-Win64-Shipping";
        const int Ready = 1 << 0;
        const int Started = 1 << 1;
        const int Split = 1 << 2;
        const int Attached = 1 << 3;
        const int StartCanceled = 1 << 5;
        const int Error = 1 << 30;
        const uint MEM_COMMIT = 0x1000;
        const uint MEM_RESERVE = 0x2000;
        const uint PAGE_EXECUTE_READWRITE = 0x40;

        static readonly HashSet<string> CompletedSplits = new HashSet<string>();
        static string baseDir;
        static string componentDir;
        static bool assemblyResolverAdded;
        static bool resolverWatchAdded;
        static dynamic uhara;
        static dynamic utils;
        static dynamic eventsTool;
        static dynamic resolver;
        static Assembly uharaAssembly;
        static Process process;

        static IntPtr startFlagPtr;
        static IntPtr splitFlagPtr;
        static IntPtr menuMainMenuPtr;
        static IntPtr menuStorySelectPtr;
        static IntPtr menuStoryButtonPtr;
        static IntPtr menuBackgroundPtr;

        static uint lastStartFlagValue;
        static uint lastSplitFlagValue;
        static uint lastMenuMainMenuValue;
        static uint lastMenuStorySelectValue;
        static uint lastMenuStoryButtonValue;
        static uint lastMenuBackgroundValue;

        static string worldName = "";
        static string lastError = "";

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool ReadProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int dwSize, out IntPtr lpNumberOfBytesRead);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern IntPtr VirtualAllocEx(IntPtr hProcess, IntPtr lpAddress, UIntPtr dwSize, uint flAllocationType, uint flProtect);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool WriteProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int dwSize, out IntPtr lpNumberOfBytesWritten);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern IntPtr CreateRemoteThread(IntPtr hProcess, IntPtr lpThreadAttributes, uint dwStackSize, IntPtr lpStartAddress, IntPtr lpParameter, uint dwCreationFlags, out uint lpThreadId);

        public static int Poll(string config)
        {
            try
            {
                Configure();
                EnsureLoaded();
                EnsureProcess();

                if (process == null || process.HasExited)
                    return Ready;

                EnsureTools();
                AddWorldWatcher();
                uhara.Update();
                UpdateWorldName();

                int flags = Ready | Attached;

                if (ReadEventFlag(startFlagPtr, ref lastStartFlagValue) && IsStartWorld(worldName))
                {
                    CompletedSplits.Clear();
                    flags |= Started;
                }

                if (ReadEventFlag(splitFlagPtr, ref lastSplitFlagValue))
                {
                    if (CompletedSplits.Add(worldName ?? ""))
                        flags |= Split;
                }

                bool menuEventThisPoll = false;
                menuEventThisPoll |= ReadEventFlag(menuMainMenuPtr, ref lastMenuMainMenuValue);
                menuEventThisPoll |= ReadEventFlag(menuStorySelectPtr, ref lastMenuStorySelectValue);
                menuEventThisPoll |= ReadEventFlag(menuStoryButtonPtr, ref lastMenuStoryButtonValue);
                menuEventThisPoll |= ReadEventFlag(menuBackgroundPtr, ref lastMenuBackgroundValue);

                if (menuEventThisPoll || IsMenuWorld(worldName))
                    flags |= StartCanceled;

                return flags;
            }
            catch (Exception ex)
            {
                lastError = ex.GetType().Name + ": " + ex.Message;
                return Error;
            }
        }

        public static int LastErrorLength(string unused)
        {
            return string.IsNullOrEmpty(lastError) ? 0 : lastError.Length;
        }

        static void Configure()
        {
            if (baseDir != null)
                return;

            baseDir = AssemblyDirectory();
            componentDir = Path.Combine(baseDir, "Components");
            if (!assemblyResolverAdded)
            {
                AppDomain.CurrentDomain.AssemblyResolve += ResolveAssembly;
                assemblyResolverAdded = true;
            }
        }

        static string AssemblyDirectory()
        {
            string location = typeof(AutoSplitBridge).Assembly.Location;
            if (!string.IsNullOrEmpty(location))
                return Path.GetDirectoryName(location);

            return AppDomain.CurrentDomain.BaseDirectory;
        }

        static Assembly ResolveAssembly(object sender, ResolveEventArgs args)
        {
            string dllName = new AssemblyName(args.Name).Name + ".dll";
            string componentPath = Path.Combine(componentDir, dllName);
            if (File.Exists(componentPath))
                return Assembly.LoadFrom(componentPath);

            string basePath = Path.Combine(baseDir, dllName);
            if (File.Exists(basePath))
                return Assembly.LoadFrom(basePath);

            return null;
        }

        static void EnsureLoaded()
        {
            if (uhara != null)
                return;

            string uharaPath = Path.Combine(componentDir, "uhara10");
            uharaAssembly = Assembly.Load(File.ReadAllBytes(uharaPath));
            uhara = uharaAssembly.CreateInstance("Main");
            resolver = uharaAssembly.CreateInstance("PtrResolver");
            WireMemoryCallbacks();
        }

        static void EnsureProcess()
        {
            if (process != null && !process.HasExited)
                return;

            Process[] found = Process.GetProcessesByName(ProcessName);
            if (found.Length == 0)
            {
                process = null;
                resolverWatchAdded = false;
                return;
            }

            process = found[0];
            uhara.SetProcess(process);
            SetMainProcess(process);
            ResetTools();
        }

        static void ResetTools()
        {
            utils = null;
            eventsTool = null;
            resolverWatchAdded = false;
            worldName = "";
            CompletedSplits.Clear();

            startFlagPtr = IntPtr.Zero;
            splitFlagPtr = IntPtr.Zero;
            menuMainMenuPtr = IntPtr.Zero;
            menuStorySelectPtr = IntPtr.Zero;
            menuStoryButtonPtr = IntPtr.Zero;
            menuBackgroundPtr = IntPtr.Zero;

            lastStartFlagValue = 0;
            lastSplitFlagValue = 0;
            lastMenuMainMenuValue = 0;
            lastMenuStorySelectValue = 0;
            lastMenuStoryButtonValue = 0;
            lastMenuBackgroundValue = 0;
        }

        static void EnsureTools()
        {
            if (utils != null && eventsTool != null)
                return;

            utils = uharaAssembly.CreateInstance("Tools+UnrealEngine+Default+Utilities");
            eventsTool = uharaAssembly.CreateInstance("Tools+UnrealEngine+Default+Events");

            startFlagPtr = eventsTool.FunctionFlag("LOADING_C", "", "Destruct");
            splitFlagPtr = eventsTool.FunctionFlag("*Timer_C", "", "AllObjectivesComplete_Event");
            menuMainMenuPtr = eventsTool.FunctionFlag("MainMenu_C", null, "*");
            menuStorySelectPtr = eventsTool.FunctionFlag("StoryLevelSelect_C", null, "*");
            menuStoryButtonPtr = eventsTool.FunctionFlag("StoryLevelButton_C", null, "*");
            menuBackgroundPtr = eventsTool.FunctionFlag("Menu_Background_C", null, "*");

            lastStartFlagValue = ReadFlagValue(startFlagPtr);
            lastSplitFlagValue = ReadFlagValue(splitFlagPtr);
            lastMenuMainMenuValue = ReadFlagValue(menuMainMenuPtr);
            lastMenuStorySelectValue = ReadFlagValue(menuStorySelectPtr);
            lastMenuStoryButtonValue = ReadFlagValue(menuStoryButtonPtr);
            lastMenuBackgroundValue = ReadFlagValue(menuBackgroundPtr);
        }

        static void SetMainProcess(Process target)
        {
            Type mainType = uhara.GetType();
            const BindingFlags flags = BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public;

            PropertyInfo prop = mainType.GetProperty("ProcessInstance", flags);
            if (prop != null)
                prop.SetValue(null, target, null);

            FieldInfo field = mainType.GetField("bf_ProcessInstance", flags);
            if (field != null)
                field.SetValue(null, target);
        }

        static void WireMemoryCallbacks()
        {
            Type mainType = uhara.GetType();
            const BindingFlags flags = BindingFlags.Static | BindingFlags.NonPublic;
            Type bridgeType = typeof(AutoSplitBridge);

            SetMethod(mainType, flags, "_RefAllocateMemory", bridgeType.GetMethod("RefAllocateMemory", BindingFlags.Static | BindingFlags.NonPublic));
            SetMethod(mainType, flags, "_RefReadBytes", bridgeType.GetMethod("RefReadBytes", BindingFlags.Static | BindingFlags.NonPublic));
            SetMethod(mainType, flags, "_RefWriteBytes", bridgeType.GetMethod("RefWriteBytes", BindingFlags.Static | BindingFlags.NonPublic));
            SetMethod(mainType, flags, "_RefCreateThread", bridgeType.GetMethod("RefCreateThread", BindingFlags.Static | BindingFlags.NonPublic));
        }

        static void SetMethod(Type targetType, BindingFlags flags, string fieldName, MethodInfo method)
        {
            FieldInfo field = targetType.GetField(fieldName, flags);
            if (field != null && method != null)
                field.SetValue(null, method);
        }

        static IntPtr RefAllocateMemory(Process target, int size)
        {
            return VirtualAllocEx(target.Handle, IntPtr.Zero, new UIntPtr((uint)size), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        }

        static byte[] RefReadBytes(Process target, IntPtr address, int count)
        {
            byte[] buffer = new byte[count];
            IntPtr read;
            ReadProcessMemory(target.Handle, address, buffer, count, out read);
            return buffer;
        }

        static void RefWriteBytes(Process target, IntPtr address, byte[] bytes)
        {
            IntPtr written;
            WriteProcessMemory(target.Handle, address, bytes, bytes.Length, out written);
        }

        static object RefCreateThread(Process target, IntPtr address)
        {
            uint threadId;
            return CreateRemoteThread(target.Handle, IntPtr.Zero, 0, address, IntPtr.Zero, 0, out threadId);
        }

        static void AddWorldWatcher()
        {
            if (resolverWatchAdded)
                return;

            resolver.Watch<uint>("WorldFName", utils.GWorld, 0x18);
            resolverWatchAdded = true;
        }

        static void UpdateWorldName()
        {
            object fName = resolver["WorldFName"];
            string name = Convert.ToString(utils.FNameToString(fName));
            if (!string.IsNullOrEmpty(name) && name != "None")
                worldName = name;
        }

        static uint ReadFlagValue(IntPtr ptr)
        {
            if (process == null || process.HasExited || ptr == IntPtr.Zero)
                return 0;

            byte[] buffer = new byte[4];
            IntPtr read;
            if (!ReadProcessMemory(process.Handle, ptr, buffer, buffer.Length, out read) || read.ToInt64() != buffer.Length)
                return 0;

            return BitConverter.ToUInt32(buffer, 0);
        }

        static bool ReadEventFlag(IntPtr ptr, ref uint lastValue)
        {
            uint value = ReadFlagValue(ptr);
            if (value == lastValue)
                return false;

            lastValue = value;
            return value != 0;
        }

        static bool IsStartWorld(string name)
        {
            return !IsMenuWorld(name) &&
                   name != "SPobby" &&
                   name != "Entry" &&
                   name != "Startup" &&
                   name != "Introduction";
        }

        static bool IsMenuWorld(string name)
        {
            if (string.IsNullOrEmpty(name))
                return false;

            return name.IndexOf("StoryLevelSelect", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("StoryLevelButton", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("Story_Category_Selection_Button", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("MainStoryMenu", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("MainMenu", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("MainChild", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("Menu_Background", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("World:Startup", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("World:Menu_Background", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("/Game/Maps/Menus/", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("TestSPLobby", StringComparison.OrdinalIgnoreCase) >= 0 ||
                   name.IndexOf("SPLobbyGmode", StringComparison.OrdinalIgnoreCase) >= 0;
        }
    }
}

using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

internal sealed class AudioDevice
{
    internal string Id;
    internal string Name;
    internal string Description;
    internal string Adapter;
    internal uint State;

    internal bool IsCable
    {
        get
        {
            return IsAdapter("VB-Audio Virtual Cable") || IsAdapter("VB-Audio Virtual Cable (WDM)");
        }
    }

    internal bool IsMultichannelCable
    {
        get { return HasName("CABLE In 16ch"); }
    }

    internal bool IsStandardCableName
    {
        get { return HasName("CABLE Input") || HasName("NovaMix Chat"); }
    }

    private bool HasName(string name)
    {
        return string.Equals(Description, name, StringComparison.OrdinalIgnoreCase) ||
            string.Equals(Name, name, StringComparison.OrdinalIgnoreCase) ||
            (Name != null && Name.StartsWith(name + " (", StringComparison.OrdinalIgnoreCase));
    }

    private bool IsAdapter(string name)
    {
        return string.Equals(Adapter, name, StringComparison.OrdinalIgnoreCase) ||
            (Name != null && Name.EndsWith("(" + name + ")", StringComparison.OrdinalIgnoreCase));
    }

    public override string ToString() { return Name; }
}

internal static class AudioDevices
{
    internal const uint Active = 1;
    internal const uint Disabled = 2;
    private const string DeviceFormat = "A45C254E-DF1C-4EFD-8020-67D146A850E0";
    [StructLayout(LayoutKind.Sequential)]
    private struct PropertyKey
    {
        internal Guid Format;
        internal uint Property;
        internal PropertyKey(string format, uint property)
        {
            Format = new Guid(format);
            Property = property;
        }
    }

    [StructLayout(LayoutKind.Explicit, Size = 24)]
    private struct PropertyValue
    {
        [FieldOffset(0)] internal ushort Type;
        [FieldOffset(8)] internal IntPtr Value;
    }

    [ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IDeviceEnumerator
    {
        void EnumAudioEndpoints(int flow, uint state, out IDeviceCollection devices);
        [PreserveSig] int GetDefaultAudioEndpoint(int flow, int role, out IDevice device);
    }

    [ComImport, Guid("0BD7A1BE-7A1A-44DB-8397-CC5392387B5E"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IDeviceCollection
    {
        void GetCount(out uint count);
        void Item(uint index, out IDevice device);
    }

    [ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IDevice
    {
        void Activate(ref Guid iid, uint context, IntPtr activation, [MarshalAs(UnmanagedType.IUnknown)] out object result);
        void OpenPropertyStore(uint mode, out IPropertyStore store);
        void GetId(out IntPtr id);
        void GetState(out uint state);
    }

    [ComImport, Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IPropertyStore
    {
        void GetCount(out uint count);
        void GetAt(uint index, out PropertyKey key);
        [PreserveSig] int GetValue(ref PropertyKey key, out PropertyValue value);
    }

    [DllImport("ole32.dll")]
    private static extern int PropVariantClear(ref PropertyValue value);

    private static string Read(IPropertyStore store, string format, uint property)
    {
        var key = new PropertyKey(format, property);
        PropertyValue value;
        int result = store.GetValue(ref key, out value);
        try
        {
            return result >= 0 && value.Type == 31 ? Marshal.PtrToStringUni(value.Value) : null;
        }
        finally { PropVariantClear(ref value); }
    }

    // Private Windows audio policy interface. The slot order matters, including unused methods.
    [ComImport, Guid("F8679F50-850A-41CF-9C72-430F290290C8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IAudioPolicy
    {
        void GetMixFormat();
        void GetDeviceFormat();
        void ResetDeviceFormat();
        void SetDeviceFormat();
        void GetProcessingPeriod();
        void SetProcessingPeriod();
        void GetShareMode();
        void SetShareMode();
        void GetPropertyValue();
        void SetPropertyValue([MarshalAs(UnmanagedType.LPWStr)] string endpoint, int effectsStore, ref PropertyKey key, ref PropertyValue value);
        void SetDefaultEndpoint();
        void SetEndpointVisibility([MarshalAs(UnmanagedType.LPWStr)] string endpoint, int visible);
    }

    internal static void Rename(AudioDevice device, string name)
    {
        if (string.Equals(device.Description, name, StringComparison.Ordinal)) return;
        var key = new PropertyKey(DeviceFormat, 2);
        var value = new PropertyValue { Type = 31, Value = Marshal.StringToCoTaskMemUni(name) };
        try { WithPolicy(policy => policy.SetPropertyValue(device.Id, 0, ref key, ref value)); }
        finally { Marshal.FreeCoTaskMem(value.Value); }
    }

    internal static void SetCableVisibility(AudioDevice device, bool visible)
    {
        if (!device.IsCable || !device.IsMultichannelCable)
            throw new InvalidOperationException(Ui.Text("Seleziona l’uscita CABLE In 16ch.", "Select the CABLE In 16ch output."));
        if (!visible && IsDefaultOutput(device.Id))
            throw new InvalidOperationException(Ui.Text("CABLE In 16ch è un’uscita predefinita di Windows. Seleziona un’altra uscita prima di nasconderla.", "CABLE In 16ch is a Windows default output. Select another default before hiding it."));
        WithPolicy(policy => policy.SetEndpointVisibility(device.Id, visible ? 1 : 0));
    }

    private static void WithPolicy(Action<IAudioPolicy> action)
    {
        var type = Type.GetTypeFromCLSID(new Guid("870AF99C-171D-4F9E-AF0D-E63DF40C2BC9"));
        object instance = Activator.CreateInstance(type);
        try { action((IAudioPolicy)instance); }
        finally { Marshal.ReleaseComObject(instance); }
    }

    private static IDeviceEnumerator CreateEnumerator()
    {
        var type = Type.GetTypeFromCLSID(new Guid("BCDE0395-E52F-467C-8E3D-C4579291692E"));
        return (IDeviceEnumerator)Activator.CreateInstance(type);
    }

    private static bool IsDefaultOutput(string id)
    {
        var enumerator = CreateEnumerator();
        try
        {
            for (int role = 0; role < 3; ++role)
            {
                IDevice device;
                int result = enumerator.GetDefaultAudioEndpoint(0, role, out device);
                if (result == unchecked((int)0x80070490)) continue; // No default for this role.
                Marshal.ThrowExceptionForHR(result);
                try { if (ReadId(device) == id) return true; }
                finally { Marshal.ReleaseComObject(device); }
            }
            return false;
        }
        finally { Marshal.ReleaseComObject(enumerator); }
    }

    internal static List<AudioDevice> Enumerate(uint stateMask = Active)
    {
        var result = new List<AudioDevice>();
        var enumerator = CreateEnumerator();
        try
        {
            IDeviceCollection devices;
            enumerator.EnumAudioEndpoints(0, stateMask, out devices);
            try
            {
                uint count;
                devices.GetCount(out count);
                for (uint i = 0; i < count; ++i)
                {
                    IDevice device;
                    devices.Item(i, out device);
                    try { result.Add(ReadDevice(device)); }
                    catch (COMException) { /* The endpoint can disappear while enumerating. */ }
                    finally { Marshal.ReleaseComObject(device); }
                }
            }
            finally { Marshal.ReleaseComObject(devices); }
        }
        finally { Marshal.ReleaseComObject(enumerator); }
        result.Sort((left, right) => StringComparer.CurrentCultureIgnoreCase.Compare(left.Name, right.Name));
        return result;
    }

    private static string ReadId(IDevice device)
    {
        IntPtr id;
        device.GetId(out id);
        try { return Marshal.PtrToStringUni(id); }
        finally { Marshal.FreeCoTaskMem(id); }
    }

    private static AudioDevice ReadDevice(IDevice device)
    {
        uint state;
        device.GetState(out state);
        IPropertyStore store;
        device.OpenPropertyStore(0, out store);
        try
        {
            return new AudioDevice
            {
                Id = ReadId(device),
                State = state,
                Name = Read(store, DeviceFormat, 14) ?? Ui.Text("Uscita audio", "Audio output"),
                Description = Read(store, DeviceFormat, 2),
                Adapter = Read(store, "026E516E-B814-414B-83CD-856D6FEF4822", 2)
            };
        }
        finally { Marshal.ReleaseComObject(store); }
    }
}

internal sealed class AudioRoutes
{
    private readonly string path;
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern uint GetPrivateProfileString(string section, string key, string fallback, StringBuilder output, uint size, string path);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool WritePrivateProfileString(string section, string key, string value, string path);

    internal AudioRoutes(string folder) { path = Path.Combine(folder, "streams.ini"); }

    internal string Get(string key)
    {
        var value = new StringBuilder(512);
        GetPrivateProfileString("NovaMix", key, "", value, (uint)value.Capacity, path);
        return value.ToString();
    }

    internal void Save(AudioDevice pc, AudioDevice chat, AudioDevice output)
    {
        // Update a temporary copy so that a failed write cannot leave partial routes.
        string pending = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            if (File.Exists(path)) File.Copy(path, pending);
            else File.WriteAllText(pending, "[NovaMix]\r\n", Encoding.Unicode);
            if (!WritePrivateProfileString("NovaMix", "routePc", pc.Id, pending) ||
                !WritePrivateProfileString("NovaMix", "routeChat", chat.Id, pending) ||
                !WritePrivateProfileString("NovaMix", "routeOut", output.Id, pending))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            if (File.Exists(path)) File.Replace(pending, path, null);
            else File.Move(pending, path);
        }
        finally { if (File.Exists(pending)) File.Delete(pending); }
    }
}

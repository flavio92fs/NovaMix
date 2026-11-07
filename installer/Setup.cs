using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Globalization;
using System.Runtime.InteropServices;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using Microsoft.Win32;

internal static class Ui
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern uint GetPrivateProfileInt(string section, string key, int fallback, string file);

    internal static bool English = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName != "it";

    internal static void UsePreferences(string folder)
    {
        English = GetPrivateProfileInt("NovaMix", "language", English ? 1 : 0, Path.Combine(folder, "streams.ini")) != 0;
    }

    internal static string Text(string italian, string english) { return English ? english : italian; }
}

internal static class Program
{
    internal const string AppName = "NovaMix 0.20";
    internal static readonly string InstallFolder = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", "NovaMix", "0.20");

    [STAThread]
    private static int Main(string[] args)
    {
        try
        {
            // Read-only audio check and isolated extraction for build validation.
            if (args.Length == 2 && args[0] == "--inspect-audio")
            {
                File.WriteAllLines(args[1], AudioDevices.Enumerate().Select(d => d.Name + " | " + d.Adapter + " | " + d.Description));
                return 0;
            }
#if !GUIDE_ONLY
            if (args.Length == 2 && args[0] == "--verify-package")
            {
                DeployApp(Path.Combine(args[1], "app"));
                ExtractResource("NovaMix.VBCable.zip", Path.Combine(args[1], "vendor"));
                return 0;
            }
#endif
#if GUIDE_ONLY
            Ui.UsePreferences(AppDomain.CurrentDomain.BaseDirectory);
#endif
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
#if GUIDE_ONLY
            const string instanceName = @"Local\NovaMix020AudioGuide";
#else
            const string instanceName = @"Local\NovaMix020Setup";
#endif
            bool first;
            using (var mutex = new Mutex(true, instanceName, out first))
            {
                if (!first) return 1;
#if GUIDE_ONLY
                Application.Run(new AudioGuide(AppDomain.CurrentDomain.BaseDirectory));
#else
                Application.Run(new SetupWindow());
#endif
            }
            return 0;
        }
        catch (Exception error)
        {
            if (args.Length == 0) MessageBox.Show(error.Message, AppName, MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
    }

    internal static void Open(string target)
    {
        Process.Start(new ProcessStartInfo(target) { UseShellExecute = true });
    }

    internal static bool IsNovaMixRunning()
    {
        var instances = Process.GetProcessesByName("NovaMix");
        try { return instances.Length != 0; }
        finally { foreach (var instance in instances) instance.Dispose(); }
    }

#if !GUIDE_ONLY
    internal static bool HasCableDriver()
    {
        using (var service = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Services\VBAudioVACMME"))
            return service != null;
    }

    private static ZipArchive OpenPackage(string name)
    {
        var data = Assembly.GetExecutingAssembly().GetManifestResourceStream(name);
        return new ZipArchive(data, ZipArchiveMode.Read);
    }

    private static void ExtractResource(string name, string path)
    {
        using (var archive = OpenPackage(name))
            archive.ExtractToDirectory(path);
    }

    internal static void DeployApp(string folder)
    {
        if (Directory.Exists(folder))
        {
            if (!File.Exists(Path.Combine(folder, "NovaMix.exe")) ||
                !File.Exists(Path.Combine(folder, "frontend", "NovaMixUi.exe")))
                throw new IOException(Ui.Text("La cartella di installazione esiste già ma è incompleta: ", "The installation folder already exists and is incomplete: ") + folder);
            using (var archive = OpenPackage("NovaMix.App.zip"))
            {
                ReplaceAppFile(archive, folder, "Configura audio NovaMix.exe");
                ReplaceAppFile(archive, folder, "NovaMix.exe");
                foreach (var asset in new[] { "ui/index.html", "ui/app.js", "ui/style.css" })
                    ReplaceAppFile(archive, folder, asset);
                ReplaceFrontend(archive, folder);
            }
            return;
        }
        Directory.CreateDirectory(Path.GetDirectoryName(folder));
        string pending = folder + "-" + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            ExtractResource("NovaMix.App.zip", pending);
            Directory.Move(pending, folder);
        }
        finally { if (Directory.Exists(pending)) Directory.Delete(pending, true); }
    }

    private static void ReplaceFrontend(ZipArchive archive, string folder)
    {
        const string prefix = "frontend/";
        string installed = Path.Combine(folder, "frontend");
        string packagedVersion;
        using (var reader = new StreamReader(archive.GetEntry(prefix + "version").Open()))
            packagedVersion = reader.ReadToEnd().Trim();
        string installedVersion = Path.Combine(installed, "version");
        if (File.Exists(installedVersion) && File.ReadAllText(installedVersion).Trim() == packagedVersion)
        {
            foreach (var asset in new[] { "main.cjs", "preload.cjs", "package.json" })
                ReplaceAppFile(archive, folder, prefix + "resources/app/" + asset);
            return;
        }

        // Unpack before replacing the working runtime. Preferences are outside frontend.
        string staging = Path.GetFullPath(Path.Combine(folder, "frontend-" + Guid.NewGuid().ToString("N") + ".tmp"));
        string backup = staging + ".old";
        string stagingRoot = staging + Path.DirectorySeparatorChar;
        Directory.CreateDirectory(staging);
        try
        {
            foreach (var entry in archive.Entries.Where(e => e.Name.Length != 0 && e.FullName.StartsWith(prefix, StringComparison.Ordinal)))
            {
                string path = Path.GetFullPath(Path.Combine(staging, entry.FullName.Substring(prefix.Length)));
                if (!path.StartsWith(stagingRoot, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("Invalid Electron package path.");
                Directory.CreateDirectory(Path.GetDirectoryName(path));
                entry.ExtractToFile(path);
            }
            Directory.Move(installed, backup);
            try { Directory.Move(staging, installed); }
            catch { Directory.Move(backup, installed); throw; }
            Directory.Delete(backup, true);
        }
        finally { if (Directory.Exists(staging)) Directory.Delete(staging, true); }
    }

    private static void ReplaceAppFile(ZipArchive archive, string folder, string name)
    {
        string destination = Path.Combine(folder, name);
        string pending = destination + "-" + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(destination));
            archive.GetEntry(name).ExtractToFile(pending);
            if (File.Exists(destination)) File.Replace(pending, destination, null);
            else File.Move(pending, destination);
        }
        catch (IOException error)
        {
            if ((error.HResult & 0xffff) == 32 || (error.HResult & 0xffff) == 33 || (error.HResult & 0xffff) == 5)
                throw new IOException(Ui.Text("Impossibile aggiornare ", "Could not update ") + name + Ui.Text(". Chiudi NovaMix dall’icona vicino all’orologio e chiudi Configura audio, poi riprova.", ". Quit NovaMix from the tray, close Audio Setup, and try again."), error);
            throw;
        }
        finally { if (File.Exists(pending)) File.Delete(pending); }
    }

    internal static void CreateShortcuts()
    {
        string programs = Environment.GetFolderPath(Environment.SpecialFolder.Programs);
        dynamic shell = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell"));
        CreateShortcut(shell, programs, AppName, "NovaMix.exe");
        CreateShortcut(shell, programs, Ui.Text("Configura audio NovaMix", "NovaMix Audio Setup"), "Configura audio NovaMix.exe");
    }

    private static void CreateShortcut(dynamic shell, string programs, string name, string executable)
    {
        dynamic shortcut = shell.CreateShortcut(Path.Combine(programs, name + ".lnk"));
        shortcut.TargetPath = Path.Combine(InstallFolder, executable);
        shortcut.WorkingDirectory = InstallFolder;
        shortcut.Save();
    }

    internal static void InstallCable()
    {
        string pending = Path.Combine(Path.GetTempPath(), "NovaMix-VBCable-" + Guid.NewGuid().ToString("N"));
        try
        {
            ExtractResource("NovaMix.VBCable.zip", pending);
            using (var setup = Process.Start(new ProcessStartInfo(Path.Combine(pending, "VBCABLE_Setup_x64.exe"))
            {
                UseShellExecute = true,
                Verb = "runas",
                WorkingDirectory = pending
            }))
            {
                setup.WaitForExit();
                if (setup.ExitCode != 0 && setup.ExitCode != 3010)
                    throw new InvalidOperationException(Ui.Text("Il setup VB-CABLE ha restituito il codice ", "VB-CABLE Setup returned code ") + setup.ExitCode + ".");
            }
        }
        finally { if (Directory.Exists(pending)) Directory.Delete(pending, true); }
    }
#endif
}

internal abstract class WizardWindow : Form
{
    protected readonly FlowLayoutPanel Content;

    protected WizardWindow(string title, int height)
    {
        Text = title;
        Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
        Font = new Font("Segoe UI", 10);
        AutoScaleMode = AutoScaleMode.Font;
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(625, height);
        MinimumSize = new Size(550, 400);
        MaximizeBox = false;
        BackColor = Color.White;
        Content = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false,
            AutoScroll = true, Padding = new Padding(24, 18, 14, 16)
        };
        Controls.Add(Content);
    }

    protected Label AddText(string text, bool title = false)
    {
        var label = new Label { Text = text, AutoSize = true, MaximumSize = new Size(550, 0) };
        if (title) label.Font = new Font(Font.FontFamily, 21, FontStyle.Bold);
        label.Margin = new Padding(0, 0, 0, title ? 12 : 8);
        Content.Controls.Add(label);
        return label;
    }

    protected Button AddButton(string text, Action action)
    {
        var button = new Button { Text = text, AutoSize = true, Padding = new Padding(8, 2, 8, 2), Margin = new Padding(0, 0, 8, 8) };
        button.Click += (sender, args) => Run(action);
        Content.Controls.Add(button);
        return button;
    }

    protected void Run(Action action)
    {
        try { action(); }
        catch (Exception error) { MessageBox.Show(this, error.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Warning); }
    }
}

#if !GUIDE_ONLY
internal sealed class SetupWindow : WizardWindow
{
    private readonly Label status;
    private readonly CheckBox cable;
    private readonly Button install;
    private readonly Button guide;
    private bool busy;

    internal SetupWindow() : base(Ui.Text("Installazione NovaMix", "NovaMix Setup"), 335)
    {
        AddText(Ui.Text("Installa NovaMix", "Install NovaMix"), true);
        AddText(Ui.Text("NovaMix 0.20 verrà installato per il tuo utente. Il menu Start includerà NovaMix e Configura audio NovaMix.", "NovaMix will be installed for your Windows account. NovaMix and NovaMix Audio Setup will appear in the Start menu."));
        bool installed = Program.HasCableDriver();
        cable = new CheckBox
        {
            Text = installed ? Ui.Text("VB-CABLE risulta già installato", "VB-CABLE is already installed") : Ui.Text("Installa VB-CABLE per il flusso Chat", "Install VB-CABLE for the Chat stream"),
            AutoSize = true, Checked = true, Enabled = !installed, Margin = new Padding(0, 0, 0, 8)
        };
        Content.Controls.Add(cable);
        var origin = new LinkLabel
        {
            Text = Ui.Text("VB-CABLE di VB-Audio · Donationware · www.vb-cable.com", "VB-CABLE by VB-Audio · Donationware · www.vb-cable.com"),
            AutoSize = true, Margin = new Padding(0, 0, 0, 14)
        };
        origin.LinkClicked += (sender, args) => Run(() => Program.Open("https://vb-audio.com/Cable/"));
        Content.Controls.Add(origin);
        install = AddButton(Ui.Text("Installa", "Install"), Install);
        guide = AddButton(Ui.Text("Configura audio", "Audio Setup"), () => Program.Open(Path.Combine(Program.InstallFolder, "Configura audio NovaMix.exe")));
        guide.Enabled = Directory.Exists(Program.InstallFolder);
        status = AddText("");
        FormClosing += (sender, args) => { if (busy) args.Cancel = true; };
    }

    private async void Install()
    {
        busy = true;
        install.Enabled = cable.Enabled = guide.Enabled = false;
        status.Text = Ui.Text("Copio NovaMix…", "Installing NovaMix…");
        try
        {
            if (Program.IsNovaMixRunning())
                throw new InvalidOperationException(Ui.Text("Chiudi NovaMix dall’icona vicino all’orologio, poi riprova.", "Quit NovaMix from the tray and try again."));
            bool wantsCable = cable.Checked && !Program.HasCableDriver();
            await Task.Run(() => Program.DeployApp(Program.InstallFolder));
            Program.CreateShortcuts();
            guide.Enabled = true;
            if (!wantsCable)
            {
                status.Text = Ui.Text("NovaMix è installato. Apri Configura audio per continuare.", "NovaMix is installed. Open Audio Setup to continue.");
                return;
            }
            status.Text = Ui.Text("Conferma il setup ufficiale VB-Audio e premi Install Driver.", "Open the official VB-Audio setup and select Install Driver.");
            await Task.Run((Action)Program.InstallCable);
            status.Text = Program.HasCableDriver()
                ? Ui.Text("VB-CABLE installato. Riavvia Windows, poi apri Configura audio NovaMix dal menu Start.", "VB-CABLE is installed. Restart Windows, then open NovaMix Audio Setup from Start.")
                : Ui.Text("NovaMix è installato. VB-CABLE non risulta ancora registrato. Riapri il setup se hai annullato l’installazione.", "NovaMix is installed. Windows has not registered VB-CABLE yet. Run setup again if you canceled driver installation.");
        }
        catch (Exception error)
        {
            status.Text = error is Win32Exception && ((Win32Exception)error).NativeErrorCode == 1223
                ? Ui.Text("Installazione di VB-CABLE annullata.", "VB-CABLE installation was canceled.") : error.Message;
        }
        finally
        {
            busy = false;
            install.Enabled = true;
            cable.Enabled = !Program.HasCableDriver();
        }
    }
}
#endif

internal sealed class AudioGuide : WizardWindow
{
    private readonly string folder;
    private readonly AudioRoutes routes;
    private readonly ComboBox pc;
    private readonly ComboBox chat;
    private readonly ComboBox output;
    private readonly Label chatStatus;
    private readonly Button rename;
    private readonly Button cableVisibility;
    private readonly Button save;
    private AudioDevice multichannelCable;

    internal AudioGuide(string folder) : base(Ui.Text("Configura audio NovaMix", "NovaMix Audio Setup"), 630)
    {
        this.folder = folder;
        routes = new AudioRoutes(folder);
        AddText(Ui.Text("Configura l’audio", "Set up audio"), true);
        AddText(Ui.Text("1. Scegli i flussi PC e Chat e dove ascoltare. Lascia libera la presa digitale se la usi come flusso PC.", "1. Choose the PC stream, the Chat stream and your playback device. Leave the digital port free if it is your PC stream."));
        pc = AddDevicePicker(Ui.Text("Flusso PC", "PC stream"));
        chat = AddDevicePicker(Ui.Text("Flusso Chat", "Chat stream"));
        output = AddDevicePicker(Ui.Text("Riproduci su", "Play through"));
        chatStatus = AddText("");
        AddButton(Ui.Text("Aggiorna dispositivi", "Refresh devices"), RefreshDevices);
        AddText(Ui.Text("2. Personalizza i nomi e la lista delle uscite di Windows.", "2. Customize the output names and device list in Windows."));
        rename = AddButton(Ui.Text("Rinomina in «NovaMix PC» e «NovaMix Chat»", "Rename to “NovaMix PC” and “NovaMix Chat”"), RenameRoutes);
        cableVisibility = AddButton(Ui.Text("Nascondi CABLE In 16ch", "Hide CABLE In 16ch"), ToggleMultichannelCable);
        AddText(Ui.Text("3. Salva e avvia NovaMix, poi premi Avvia audio. In Audio di Windows imposta il flusso PC come predefinito. Nel Mixer volume assegna il flusso Chat a Discord, WhatsApp e Telegram.", "3. Save and open NovaMix, then select Start audio. In Windows Sound, set the PC stream as the default output. In Volume mixer, assign the Chat stream to Discord, WhatsApp or Telegram."));
        AddButton(Ui.Text("Apri Audio di Windows", "Open Windows Sound"), () => Program.Open("ms-settings:sound"));
        AddButton(Ui.Text("Apri Mixer volume di Windows", "Open Windows Volume mixer"), () => Program.Open("ms-settings:apps-volume"));
        save = AddButton(Ui.Text("Salva e apri NovaMix", "Save and open NovaMix"), Save);
        chat.SelectedIndexChanged += (sender, args) => UpdateButtons();
        pc.SelectedIndexChanged += (sender, args) => UpdateButtons();
        Run(RefreshDevices);
    }

    private ComboBox AddDevicePicker(string label)
    {
        AddText(label);
        var box = new ComboBox
        {
            Width = 530, DropDownStyle = ComboBoxStyle.DropDownList, Margin = new Padding(0, 0, 0, 10),
            AccessibleName = label
        };
        Content.Controls.Add(box);
        return box;
    }

    private void RefreshDevices()
    {
        string pcId = SelectedId(pc) ?? routes.Get("routePc");
        string chatId = SelectedId(chat) ?? routes.Get("routeChat");
        string outId = SelectedId(output) ?? routes.Get("routeOut");
        var allDevices = AudioDevices.Enumerate(AudioDevices.Active | AudioDevices.Disabled);
        var extras = allDevices.Where(device => device.IsCable && device.IsMultichannelCable).ToList();
        multichannelCable = extras.Count == 1 ? extras[0] : null;
        var devices = allDevices.Where(device => device.State == AudioDevices.Active).ToList();
        var cables = devices.Where(device => device.IsCable && !device.IsMultichannelCable).ToList();
        var others = devices.Where(device => !device.IsCable).ToList();
        Fill(pc, others, pcId, device => device.Description == "NovaMix PC" ||
            device.Name.StartsWith("Uscita digitale (High Definition Audio Device", StringComparison.OrdinalIgnoreCase) ||
            device.Name.StartsWith("Digital Output (High Definition Audio Device", StringComparison.OrdinalIgnoreCase));
        Fill(chat, cables, chatId, device => cables.Count == 1 || device.IsStandardCableName);
        Fill(output, others, outId, device => device.Name.IndexOf("Arctis Nova Pro Wireless", StringComparison.OrdinalIgnoreCase) >= 0);
        chatStatus.Text = cables.Count == 0
            ? Ui.Text("CABLE Input non disponibile. Dopo l’installazione, riavvia Windows e aggiorna l’elenco.", "CABLE Input is unavailable. After installation, restart Windows and refresh the list.")
            : Ui.Text("Per la chat usa CABLE Input, oppure NovaMix Chat se l’hai già rinominato.", "For Chat, choose CABLE Input or NovaMix Chat if you already renamed it.");
        UpdateButtons();
    }

    private void UpdateButtons()
    {
        save.Enabled = chat.SelectedItem != null;
        rename.Enabled = pc.SelectedItem != null && chat.SelectedItem != null;
        cableVisibility.Enabled = multichannelCable != null;
        cableVisibility.Text = multichannelCable != null && multichannelCable.State == AudioDevices.Disabled
            ? Ui.Text("Mostra CABLE In 16ch", "Show CABLE In 16ch") : Ui.Text("Nascondi CABLE In 16ch", "Hide CABLE In 16ch");
    }

    private void RenameRoutes()
    {
        RefreshDevices();
        var source = pc.SelectedItem as AudioDevice;
        var cable = chat.SelectedItem as AudioDevice;
        if (source == null || cable == null)
            throw new InvalidOperationException(Ui.Text("Seleziona le uscite dei flussi PC e Chat.", "Select outputs for both PC and Chat."));
        try
        {
            AudioDevices.Rename(source, "NovaMix PC");
            AudioDevices.Rename(cable, "NovaMix Chat");
        }
        finally { RefreshDevices(); }
        var currentPc = pc.SelectedItem as AudioDevice;
        var currentChat = chat.SelectedItem as AudioDevice;
        if (currentPc == null || currentPc.Description != "NovaMix PC" ||
            currentChat == null || currentChat.Description != "NovaMix Chat")
            throw new InvalidOperationException(Ui.Text("Windows non ha confermato i nuovi nomi. Riprova o usa Audio di Windows.", "Windows did not confirm the new names. Try again or use Windows Sound."));
        chatStatus.Text = Ui.Text("I nomi NovaMix PC e NovaMix Chat sono stati applicati in Windows.", "Windows now shows NovaMix PC and NovaMix Chat.");
    }

    private void ToggleMultichannelCable()
    {
        RefreshDevices();
        var device = multichannelCable;
        if (device == null)
            throw new InvalidOperationException(Ui.Text("CABLE In 16ch non è identificabile in modo univoco.", "Could not identify a single CABLE In 16ch device."));
        bool visible = device.State == AudioDevices.Disabled;
        AudioDevices.SetCableVisibility(device, visible);
        RefreshDevices();
        if (multichannelCable == null || (multichannelCable.State == AudioDevices.Active) != visible)
            throw new InvalidOperationException(Ui.Text("Windows non ha confermato la modifica. Riprova o usa Audio di Windows.", "Windows did not confirm the change. Try again or use Windows Sound."));
        chatStatus.Text = visible ? Ui.Text("CABLE In 16ch è di nuovo disponibile in Windows.", "CABLE In 16ch is available again in Windows.") : Ui.Text("CABLE In 16ch è nascosto in Windows.", "CABLE In 16ch is hidden in Windows.");
    }

    private static void Fill(ComboBox box, List<AudioDevice> choices, string preferredId, Func<AudioDevice, bool> suggested)
    {
        box.Items.Clear();
        box.Items.AddRange(choices.ToArray());
        var previous = choices.FirstOrDefault(device => device.Id == preferredId);
        if (previous != null) box.SelectedItem = previous;
        else
        {
            var matches = choices.Where(suggested).ToList();
            if (matches.Count == 1) box.SelectedItem = matches[0];
        }
    }

    private static string SelectedId(ComboBox box)
    {
        var device = box.SelectedItem as AudioDevice;
        return device == null ? null : device.Id;
    }

    private void Save()
    {
        RefreshDevices();
        var source = pc.SelectedItem as AudioDevice;
        var cable = chat.SelectedItem as AudioDevice;
        var destination = output.SelectedItem as AudioDevice;
        if (source == null || destination == null || cable == null || source.Id == destination.Id)
            throw new InvalidOperationException(Ui.Text("Scegli tre uscite disponibili e distinte per PC, Chat e riproduzione.", "Choose three different available outputs for PC, Chat and playback."));
        if (Program.IsNovaMixRunning())
            throw new InvalidOperationException(Ui.Text("Chiudi NovaMix dall’icona vicino all’orologio e riprova.", "Quit NovaMix from the tray and try again."));
        routes.Save(source, cable, destination);
        Program.Open(Path.Combine(folder, "NovaMix.exe"));
        MessageBox.Show(this, Ui.Text("In NovaMix premi Avvia audio. Poi imposta ", "Select Start audio in NovaMix. Then set ") + source.Description +
            Ui.Text(" come uscita predefinita di Windows e assegna ", " as the Windows default output and assign ") + cable.Description +
            Ui.Text(" alle app di chat nel Mixer volume.", " to chat apps in Windows Volume mixer."), Text);
    }
}

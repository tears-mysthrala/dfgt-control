using Microsoft.Win32;
using System.Diagnostics;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;

namespace DfgtControl;

public partial class MainWindow : Window
{
    private readonly string profilePath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "DfgtControl", "profiles.json");
    private readonly JsonSerializerOptions jsonOptions = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };
    private ProfileDocument document = new();
    private bool loading;
    private bool agentStartAttempted;

    public MainWindow()
    {
        InitializeComponent();
        LoadProfiles();
        RefreshAgentStatus();
        var timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        timer.Tick += (_, _) => RefreshAgentStatus();
        timer.Start();
    }

    private void LoadProfiles()
    {
        try
        {
            document = JsonSerializer.Deserialize<ProfileDocument>(
                File.ReadAllText(profilePath), jsonOptions) ?? new();
        }
        catch
        {
            try
            {
                if (File.Exists(profilePath))
                {
                    var quarantined = profilePath +
                        $".invalid-{DateTime.UtcNow:yyyyMMdd-HHmmss}.json";
                    File.Move(profilePath, quarantined);
                }
            }
            catch
            {
                // Keep the invalid file untouched if it cannot be preserved.
            }
            document = new();
        }
        if (document.Profiles.Count == 0)
            document.Profiles.Add(Profile.Desktop());
        ProfilesList.ItemsSource = document.Profiles;
        ProfilesList.SelectedIndex = 0;
    }

    private void RefreshAgentStatus()
    {
        var running = Process.GetProcessesByName("DfgtAgent").Length != 0;
        if (!running && !agentStartAttempted)
        {
            agentStartAttempted = true;
            var agent = Path.Combine(AppContext.BaseDirectory, "DfgtAgent.exe");
            if (File.Exists(agent))
            {
                try
                {
                    Process.Start(new ProcessStartInfo(agent)
                    {
                        UseShellExecute = false,
                        WorkingDirectory = AppContext.BaseDirectory
                    });
                }
                catch
                {
                    // Status remains red; installation may be incomplete.
                }
                running = Process.GetProcessesByName("DfgtAgent").Length != 0;
            }
        }
        AgentStatus.Text = running ? "Agente activo" : "Agente detenido";
        AgentDot.Fill = new SolidColorBrush(
            (Color)ColorConverter.ConvertFromString(running ? "#35D6A0" : "#E96868"));
    }

    private Profile? Selected => ProfilesList.SelectedItem as Profile;

    private void ProfilesList_OnSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (Selected is not { } profile) return;
        loading = true;
        ProfileName.Text = profile.Name;
        ExecutablePath.Text = profile.ExecutablePath;
        Range.Value = profile.RangeDegrees;
        Overall.Value = profile.OverallGain / 100d;
        Boundary.Value = profile.BoundaryForce / 50d;
        Spring.Value = profile.SpringGain / 100d;
        Damper.Value = profile.DamperGain / 100d;
        Friction.Value = profile.FrictionGain / 100d;
        Periodic.Value = profile.PeriodicGain / 100d;
        loading = false;
        UpdateLabels();
    }

    private void CopyEditorTo(Profile profile)
    {
        profile.Name = string.IsNullOrWhiteSpace(ProfileName.Text)
            ? "Sin nombre" : ProfileName.Text.Trim();
        profile.ExecutablePath = ExecutablePath.Text;
        profile.RangeDegrees = (int)Range.Value;
        profile.OverallGain = (int)Overall.Value * 100;
        profile.BoundaryForce = (int)Boundary.Value * 50;
        profile.SpringGain = (int)Spring.Value * 100;
        profile.DamperGain = (int)Damper.Value * 100;
        profile.FrictionGain = (int)Friction.Value * 100;
        profile.PeriodicGain = (int)Periodic.Value * 100;
    }

    private void Save_OnClick(object sender, RoutedEventArgs e)
    {
        if (Selected is not { } profile) return;
        CopyEditorTo(profile);
        Directory.CreateDirectory(Path.GetDirectoryName(profilePath)!);
        var temporary = profilePath + ".ui.tmp";
        File.WriteAllText(temporary, JsonSerializer.Serialize(document, jsonOptions));
        File.Move(temporary, profilePath, true);
        ProfilesList.Items.Refresh();
        SaveStatus.Text = "Guardado · el agente lo aplicará automáticamente";
    }

    private void AddProfile_OnClick(object sender, RoutedEventArgs e)
    {
        if (Selected is { } current) CopyEditorTo(current);
        var profile = Profile.NewGame();
        document.Profiles.Add(profile);
        ProfilesList.Items.Refresh();
        ProfilesList.SelectedItem = profile;
    }

    private void DeleteProfile_OnClick(object sender, RoutedEventArgs e)
    {
        if (Selected is not { } profile || profile.Id == "desktop") return;
        document.Profiles.Remove(profile);
        ProfilesList.Items.Refresh();
        ProfilesList.SelectedIndex = 0;
        SaveStatus.Text = "Pulsa «Guardar y aplicar» para confirmar";
    }

    private void BrowseExecutable_OnClick(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            Filter = "Aplicaciones (*.exe)|*.exe",
            CheckFileExists = true,
            Title = "Seleccionar juego"
        };
        if (dialog.ShowDialog(this) == true)
            ExecutablePath.Text = dialog.FileName;
    }

    private void Range_OnValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (!loading) UpdateLabels();
    }

    private void Percent_OnValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (!loading) UpdateLabels();
    }

    private void UpdateLabels()
    {
        if (!IsInitialized ||
            RangeValue is null ||
            OverallValue is null ||
            BoundaryValue is null ||
            SpringValue is null ||
            DamperValue is null ||
            FrictionValue is null ||
            PeriodicValue is null) return;
        RangeValue.Text = $"{(int)Range.Value}°";
        OverallValue.Text = $"{(int)Overall.Value}%";
        BoundaryValue.Text = $"{(int)Boundary.Value}%";
        SpringValue.Text = $"{(int)Spring.Value}%";
        DamperValue.Text = $"{(int)Damper.Value}%";
        FrictionValue.Text = $"{(int)Friction.Value}%";
        PeriodicValue.Text = $"{(int)Periodic.Value}%";
    }
}

public sealed class ProfileDocument
{
    public int SchemaVersion { get; set; } = 1;
    public List<Profile> Profiles { get; set; } = [];
}

public sealed class Profile
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public string ExecutablePath { get; set; } = "";
    public int RangeDegrees { get; set; } = 900;
    public int OverallGain { get; set; } = 10000;
    public int BoundaryForce { get; set; } = 3000;
    public int SpringGain { get; set; } = 10000;
    public int DamperGain { get; set; } = 10000;
    public int FrictionGain { get; set; } = 10000;
    public int PeriodicGain { get; set; } = 10000;

    public static Profile Desktop() => new() { Id = "desktop", Name = "Escritorio" };
    public static Profile NewGame() => new()
    {
        Id = $"game-{Guid.NewGuid():N}"[..21],
        Name = "Nuevo juego",
        RangeDegrees = 540
    };
}

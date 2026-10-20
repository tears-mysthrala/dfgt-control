using System.Windows;

namespace DfgtControl;

public partial class App : Application
{
    private Mutex? instanceMutex;

    protected override void OnStartup(StartupEventArgs e)
    {
        instanceMutex = new Mutex(
            true,
            @"Local\DfgtControl.Ui.SingleInstance.v1",
            out var created);
        if (!created)
        {
            MessageBox.Show(
                "DFGT Control ya está abierto.",
                "DFGT Control",
                MessageBoxButton.OK,
                MessageBoxImage.Information);
            Shutdown();
            return;
        }
        base.OnStartup(e);
    }

    protected override void OnExit(ExitEventArgs e)
    {
        instanceMutex?.ReleaseMutex();
        instanceMutex?.Dispose();
        base.OnExit(e);
    }
}

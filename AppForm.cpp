///
/// Created by DarknessFX - https://dfx.lv - @DrkFX
/// Source Code at https://github.com/DarknessFX/UEPlugins_DisableDefault
///

#include <windows.h>
#include "AppForm.h"

using namespace System;
using namespace System::Windows::Forms;
using namespace System::IO;
                                                                                                                                                                                                                                                                                                                                                        using namespace Microsoft::Win32;
using namespace UEPluginsDisableDefault;

String^ GetEngineRootPath()
{
    DirectoryInfo^ appDir = gcnew DirectoryInfo(Application::StartupPath);

    if (String::Compare(appDir->Name, "XDevops", true) == 0 && appDir->Parent != nullptr)
    {
        return appDir->Parent->FullName;
    }

    if (appDir->Parent != nullptr &&
        String::Compare(appDir->Parent->Name, "XDevops", true) == 0 &&
        appDir->Parent->Parent != nullptr)
    {
        return appDir->Parent->Parent->FullName;
    }

    return nullptr;
}

void AppForm::AppForm_Load ( System::Object^ sender, System::EventArgs^ e )
{
    Application::EnableVisualStyles();

    StateUpdate(AppState::Wait);
    cmbUEFolder->Items->Clear();
    dtbPlugins->Clear();

    StatusUpdate("Finding Unreal Engine root from XDevops folder.");

    EngineRootPath = GetEngineRootPath();
    if (String::IsNullOrWhiteSpace(EngineRootPath) ||
        !Directory::Exists(Append(EngineRootPath, "\\Engine\\Plugins")))
    {
        MessageBox::Show(this,
            "Engine root not found. This tool must run from <UE5 ENGINE ROOT>\\XDevops or a child build folder under XDevops.",
            "UEPlugins_DisableDefault",
            MessageBoxButtons::OK,
            MessageBoxIcon::Error);
        Close();
        return;
    }

    LoadConfig();

    StatusUpdate("Finding .uplugins_backup with our backup file.");
    BackupAll();

    Searching(EngineRootPath);

    UpdateFlow();
    mnuTemplateMinimal->Visible = false;
    cmbUEFolder->Focus();
    StateUpdate(AppState::Default);
}    

void AppForm::AppForm_SizeChanged ( System::Object^ sender, System::EventArgs^ e )
{
    UpdateFlow();
}

void AppForm::UpdateFlow ( )
{
    cmbUEFolder->Width = ClientSize.Width -
        mnuStrip->Width -
        splitter1->Width -
        btnBrowse->Width -
        btnRemove->Width -
        btnRestore->Width -
        lblUEFolder->Width -
        txtSearch->Width -
        72;
}

void AppForm::LoadConfig()
{
    String^ configPath = GetConfigFilePath();
    if (!File::Exists(configPath))
    {
        return;
    }

    for each (String ^ line in File::ReadAllLines(configPath))
    {
        if (!line->StartsWith("BackupRoot=", StringComparison::CurrentCultureIgnoreCase))
        {
            continue;
        }

        String^ backupRoot = line->Substring(11)->Trim();
        if (String::IsNullOrWhiteSpace(backupRoot))
        {
            break;
        }

        cmbUEFolder->Items->Clear();
        cmbUEFolder->Items->Add(backupRoot);
        cmbUEFolder->SelectedIndex = 0;
        break;
    }
}

void AppForm::SaveConfig()
{
    String^ backupRoot = GetBackupRoot();
    String^ configPath = GetConfigFilePath();

    cli::array<String^>^ lines = gcnew cli::array<String^>(1);
    lines[0] = Append("BackupRoot=", backupRoot);
    File::WriteAllLines(configPath, lines);
}

System::String^ AppForm::GetConfigFilePath()
{
    return Append(Application::StartupPath, "\\ueplugins_config.ini");
}

System::String^ AppForm::GetBackupRoot()
{
    if (cmbUEFolder->SelectedItem != nullptr)
    {
        return cmbUEFolder->SelectedItem->ToString();
    }

    if (!String::IsNullOrWhiteSpace(cmbUEFolder->Text))
    {
        return cmbUEFolder->Text;
    }

    return "";
}

System::String^ AppForm::GetPluginRelativeDirectory(System::String^ PluginFilePath)
{
    String^ pluginDirectory = Path::GetDirectoryName(PluginFilePath);
    if (String::IsNullOrWhiteSpace(pluginDirectory))
    {
        return "";
    }

    pluginDirectory = pluginDirectory->Replace("/", "\\");
    return TrimLeadingSlash(pluginDirectory);
}

void AppForm::ReleasePluginIcons()
{
    AppForm::grdPlugins->DataSource = nullptr;

    for each (DataRow ^ row in AppForm::dtbPlugins->Rows)
    {
        if (row["celIcon"] == DBNull::Value)
        {
            continue;
        }

        Image^ iconImage = dynamic_cast<Image^>(row["celIcon"]);
        if (iconImage != nullptr)
        {
            delete iconImage;
        }

        row["celIcon"] = DBNull::Value;
    }

    for each (DataRow ^ row in AppForm::dtbPluginsOrig->Rows)
    {
        if (row["celIcon"] == DBNull::Value)
        {
            continue;
        }

        row["celIcon"] = DBNull::Value;
    }
}

void AppForm::RemoveSelectedPlugins()
{
    String^ backupRoot = GetBackupRoot();
    if (String::IsNullOrWhiteSpace(backupRoot))
    {
        MessageBox::Show(this,
            "Select a backup folder first.",
            "UEPlugins_DisableDefault",
            MessageBoxButtons::OK,
            MessageBoxIcon::Information);
        return;
    }

    Directory::CreateDirectory(backupRoot);

    StateUpdate(AppState::Wait);
    ControlsStateChange(ControlsState::Wait);
    StatusUpdate("Removing selected plugins...");

    int movedCount = 0;
    int skippedCount = 0;

    try
    {
        ReleasePluginIcons();
        for each (DataRow ^ mDRPlug in dtbPlugins->Rows)
        {
            if ((bool)mDRPlug["celOffload"] != true)
            {
                continue;
            }

            String^ relativePluginDir = GetPluginRelativeDirectory(mDRPlug["celPath"]->ToString());
            if (String::IsNullOrWhiteSpace(relativePluginDir))
            {
                skippedCount++;
                continue;
            }

            String^ sourceDir = Append(Append(EngineRootPath, "\\Engine\\Plugins\\"), relativePluginDir);
            String^ targetDir = Append(Append(backupRoot, "\\"), relativePluginDir);

            if (!Directory::Exists(sourceDir))
            {
                skippedCount++;
                continue;
            }

            String^ targetParent = Path::GetDirectoryName(targetDir);
            if (!String::IsNullOrWhiteSpace(targetParent))
            {
                Directory::CreateDirectory(targetParent);
            }

            MoveDirectorySafe(sourceDir, targetDir);
            movedCount++;
        }

        Searching(EngineRootPath);
        StatusUpdate(Append(Append(movedCount.ToString(), " plugins moved to backup. Skipped: "), skippedCount.ToString()));
    }
    catch (Exception^ ex)
    {
        StatusUpdate("Remove failed.");
        MessageBox::Show(this, ex->Message, "UEPlugins_DisableDefault", MessageBoxButtons::OK, MessageBoxIcon::Error);
    }

    ControlsStateChange(ControlsState::Default);
    StateUpdate(AppState::Default);
}

void AppForm::RestoreBackupPlugins()
{
    String^ backupRoot = GetBackupRoot();
    if (String::IsNullOrWhiteSpace(backupRoot) || !Directory::Exists(backupRoot))
    {
        MessageBox::Show(this,
            "Backup folder not found.",
            "UEPlugins_DisableDefault",
            MessageBoxButtons::OK,
            MessageBoxIcon::Information);
        return;
    }

    StateUpdate(AppState::Wait);
    ControlsStateChange(ControlsState::Wait);
    StatusUpdate("Restoring plugins from backup...");

    int restoredCount = 0;
    int skippedCount = 0;

    try
    {
        List<String^>^ pluginFiles = FindAllUPlugins(backupRoot);
        List<String^>^ pluginDirs = gcnew List<String^>();
        ReleasePluginIcons();

        for each (String ^ pluginFile in pluginFiles)
        {
            String^ pluginDir = Path::GetDirectoryName(pluginFile);
            if (!pluginDirs->Contains(pluginDir))
            {
                pluginDirs->Add(pluginDir);
            }
        }

        for each (String ^ backupPluginDir in pluginDirs)
        {
            if (!Directory::Exists(backupPluginDir))
            {
                skippedCount++;
                continue;
            }

            String^ relativePluginDir = backupPluginDir->Substring(backupRoot->Length);
            relativePluginDir = TrimLeadingSlash(relativePluginDir);

            String^ targetDir = Append(Append(EngineRootPath, "\\Engine\\Plugins\\"), relativePluginDir);
            if (Directory::Exists(targetDir))
            {
                skippedCount++;
                continue;
            }

            String^ targetParent = Path::GetDirectoryName(targetDir);
            if (!String::IsNullOrWhiteSpace(targetParent))
            {
                Directory::CreateDirectory(targetParent);
            }

            MoveDirectorySafe(backupPluginDir, targetDir);
            restoredCount++;
        }

        Searching(EngineRootPath);
        StatusUpdate(Append(Append(restoredCount.ToString(), " plugins restored. Skipped: "), skippedCount.ToString()));
    }
    catch (Exception^ ex)
    {
        StatusUpdate("Restore failed.");
        MessageBox::Show(this, ex->Message, "UEPlugins_DisableDefault", MessageBoxButtons::OK, MessageBoxIcon::Error);
    }

    ControlsStateChange(ControlsState::Default);
    StateUpdate(AppState::Default);
}

void AppForm::StatusUpdate(String^ Message)
{
    AppForm::lblStatus->Text = Message;
}

void AppForm::mnuShowAll_Click ( System::Object^ sender, System::EventArgs^ e )
{
    if (mnuShowEnabled->Checked)
    {
        mnuShowEnabled->Checked = false;
    }
    mnuShowAll->Checked = true;

    for ( int iRow = 0; iRow < AppForm::grdPlugins->Rows->Count; iRow++ )
    {
        AppForm::grdPlugins->Rows[iRow]->Visible = true;
    }
}

void AppForm::mnuShowEnabled_Click ( System::Object^ sender, System::EventArgs^ e )
{
    if (mnuShowAll->Checked) 
    {
        mnuShowAll->Checked = false;
    }
    mnuShowEnabled->Checked = true;

    CurrencyManager^ curMng = (CurrencyManager^)BindingContext[grdPlugins->DataSource];
    curMng->SuspendBinding();
    for ( int iRow = 0; iRow < AppForm::grdPlugins->Rows->Count; iRow++ )
    {
        if ((bool)AppForm::grdPlugins->Rows[iRow]->Cells[0]->Value != true) 
        {
            AppForm::grdPlugins->Rows[iRow]->Visible = false;
        }
    }
    curMng->ResumeBinding();
}

void AppForm::cmbUEFolder_SelectedIndexChanged ( System::Object^ sender, System::EventArgs^ e )
{
    SaveConfig();
}

void AppForm::btnBrowse_Click ( System::Object^ sender, System::EventArgs^ e )
{
    dlgBrowse->Description = "Select plugin backup folder";

    ::DialogResult result = dlgBrowse->ShowDialog();
    if (result == ::DialogResult::OK)
    {
        cmbUEFolder->Items->Clear();
        cmbUEFolder->Items->Add(dlgBrowse->SelectedPath);
        cmbUEFolder->SelectedIndex = 0;
        SaveConfig();
        StatusUpdate(Append("Backup folder set: ", dlgBrowse->SelectedPath));
    }
}

void AppForm::mnuTemplateMinimal_Click ( System::Object^ sender, System::EventArgs^ e ) 
{
    array<String^>^ aMinimal = {"AISupport", "ContentBrowserAssetDataSource", "ContentBrowserClassDataSource", "CurveEditorTools", "TextureFormateOodle", "OodleData", "PluginBrowser", "PluginUtils", "PropertyAccessEditor"};
    for ( int iRow = 0; iRow < AppForm::dtbPlugins->Rows->Count; iRow++ )
    {
        DataRow^ mDR = AppForm::dtbPlugins->Rows[iRow];
        mDR["celEnabledByDefault"] = false;
        for each (String^ sPlugin in aMinimal) 
        {
            if (mDR["celName"]->ToString()->ToLower() == sPlugin->ToLower() )
            {
                mDR["celEnabledByDefault"] = true;
                break;
            }
        }
    }
}

void AppForm::btnSave_Click ( System::Object^ sender, System::EventArgs^ e )
{
    StateUpdate(AppState::Wait);
    AppForm::txtSearch->Text = "";
    AppForm::dtbPlugins->DefaultView->RowFilter = "";
    StatusUpdate("Saving .uplugin changes...");
    ControlsStateChange(ControlsState::Wait);

    String^ dirPlugin = Append(EngineRootPath, "\\Engine\\Plugins\\");
    String^ filePlugin = "";

    int iMod = 0;
    for each (DataRow^ mDRPlug in dtbPlugins->Rows)
    {
        for each (DataRow^ mDROrig in dtbPluginsOrig->Rows)
        {
            if (mDRPlug["celName"] == mDROrig["celName"])
            {
                if ((bool)mDRPlug["celEnabledByDefault"] != (bool)mDROrig["celEnabledByDefault"] || 
                    (bool)mDRPlug["celInstalled"] != (bool)mDROrig["celInstalled"]) 
                {
                    filePlugin = Append(dirPlugin, mDRPlug["celPath"]->ToString());
                    if (File::Exists(Append(filePlugin, "_UEPlugins_DisableDefault"))) {
                      File::Delete(Append(filePlugin, "_UEPlugins_DisableDefault"));
                    }
                    File::Move(filePlugin, Append(filePlugin, "_UEPlugins_DisableDefault"));
                    Application::DoEvents();

                    FileStream^ origStream = gcnew FileStream(Append(filePlugin, "_UEPlugins_DisableDefault"), FileMode::Open);
                    StreamReader^ origReader = gcnew StreamReader(origStream);

                    FileStream^ newStream = gcnew FileStream(filePlugin, FileMode::CreateNew);
                    StreamWriter^ newWriter = gcnew StreamWriter(newStream);

                    bool bMissingLineEBD = !origReader->ReadToEnd()->ToLower()->Contains("enabledbydefault");
                    origReader->DiscardBufferedData();
                    origReader->BaseStream->Seek(0, SeekOrigin::Begin);

                    bool bMissingLineINS = !origReader->ReadToEnd()->ToLower()->Contains("installed");
                    origReader->DiscardBufferedData();
                    origReader->BaseStream->Seek(0, SeekOrigin::Begin);

                    while ( !origReader->EndOfStream )
                    {
                        String^ line = origReader->ReadLine();
                        if ( line->Contains("EnabledByDefault") && (bool)mDRPlug["celEnabledByDefault"] != (bool)mDROrig["celEnabledByDefault"]) 
                        {
                            line = line->Replace(mDROrig["celEnabledByDefault"]->ToString()->ToLower(), mDRPlug["celEnabledByDefault"]->ToString()->ToLower());
                        }
                        if ( line->Contains("Installed") && (bool)mDRPlug["celInstalled"] != (bool)mDROrig["celInstalled"]) 
                        {
                            line = line->Replace(mDROrig["celInstalled"]->ToString()->ToLower(), mDRPlug["celInstalled"]->ToString()->ToLower());
                        }
                        if (line->Contains("Modules")) {
                          int ident_pos = line->ToLower()->IndexOf("modules") - 1;
                          if ( bMissingLineEBD )
                          {
                            String^ identstr = gcnew String(L' ', ident_pos);
                            String^ InsertLine; 
                            InsertLine = Append(identstr, "\"EnabledByDefault\": ");
                            InsertLine = InsertLine->Insert(InsertLine->Length, mDRPlug["celEnabledByDefault"]->ToString()->ToLower());
                            InsertLine = InsertLine->Insert(InsertLine->Length, ",\n");
                            InsertLine = InsertLine->Insert(InsertLine->Length, line);
                            line = InsertLine;
                            bMissingLineEBD = false;
                          }
                          if ( bMissingLineINS )
                          {
                            String^ identstr = gcnew String(L' ', ident_pos);
                            String^ InsertLine; 
                            InsertLine = Append(identstr, "\"Installed\": ");
                            InsertLine = InsertLine->Insert(InsertLine->Length, mDRPlug["celInstalled"]->ToString()->ToLower());
                            InsertLine = InsertLine->Insert(InsertLine->Length, ",\n");
                            InsertLine = InsertLine->Insert(InsertLine->Length, line);
                            line = InsertLine;
                            bMissingLineINS = false;
                          }

                        }
                        newWriter->WriteLine(line);
                    }

                    newWriter->Close();
                    newStream->Close();
                    origReader->Close();
                    origStream->Close();
                    File::Delete(Append(filePlugin, "_UEPlugins_DisableDefault"));

                    iMod++;
                    mDROrig->Delete();
                    dtbPluginsOrig->AcceptChanges();
                }
                break;
            }
        }
    }

    dtbPluginsOrig = dtbPlugins->Copy();

    ControlsStateChange(ControlsState::Default);
    StatusUpdate(Append(iMod.ToString(), " plugins changed."));
    StateUpdate(AppState::Default);
}

void AppForm::btnRemove_Click(System::Object^ sender, System::EventArgs^ e)
{
    RemoveSelectedPlugins();
}

void AppForm::btnRestore_Click(System::Object^ sender, System::EventArgs^ e)
{
    RestoreBackupPlugins();
}

void AppForm::grdPlugins_CurrentCellDirtyStateChanged ( System::Object^ sender, System::EventArgs^ e )
{
    if ( grdPlugins->IsCurrentCellDirty ) 
    {
        grdPlugins->CommitEdit(DataGridViewDataErrorContexts::Commit);
        grdPlugins->EndEdit();
        dtbPlugins->AcceptChanges();
    }
}

void AppForm::grdPlugins_CellDoubleClick ( System::Object^ sender, DataGridViewCellEventArgs^ e )
{
  // Double-Click PATH column open Windows Explorer
  if (e->ColumnIndex == 7) {
    String^ PluginPath; 
    PluginPath = Append(EngineRootPath, "\\Engine\\Plugins\\");
    PluginPath = Append(PluginPath, grdPlugins[e->ColumnIndex, e->RowIndex]->Value->ToString());
    PluginPath = PluginPath->Substring(0, PluginPath->LastIndexOf("\\"));
    System::Diagnostics::Process::Start(PluginPath);
  }
}

void AppForm::mnuShow_MouseHover(System::Object^ sender, System::EventArgs^ e)
{
  StatusUpdate("Filter list to show All plugins or only Enabled plugins.");
}

void AppForm::mnuBackup_MouseHover(System::Object^ sender, System::EventArgs^ e)
{
  StatusUpdate("Save or Load backup of plugins state.");
}

void AppForm::mnuTemplate_MouseHover(System::Object^ sender, System::EventArgs^ e)
{
  StatusUpdate("Save or Load templates of plugins state lists.");
}

void AppForm::StateUpdate(AppState State)
{
    switch ( State ) {
        case AppState::Default:
            Application::UseWaitCursor = false;
            break;
        case AppState::Wait:
            Application::UseWaitCursor = true;
            break;
    }
}

String^ Append(String^ str0, String^ str1) 
{
    return str0->Insert(str0->Length, str1);
}

String^ ReplaceSlashes(String^ Path) 
{
    Path = Path->Replace("\\\\", "\\");
    Path = Path->Replace("\\", "/");
    return Path;
}

String^ TrimLeadingSlash(String^ Path)
{
    if (String::IsNullOrWhiteSpace(Path))
    {
        return "";
    }

    while (Path->StartsWith("\\") || Path->StartsWith("/"))
    {
        Path = Path->Substring(1);
    }

    return Path;
}

Drawing::Image^ LoadImageUnlocked(String^ FilePath)
{
    if (!File::Exists(FilePath))
    {
        return nullptr;
    }

    array<Byte>^ bytes = File::ReadAllBytes(FilePath);
    MemoryStream^ stream = gcnew MemoryStream(bytes);

    try
    {
        Image^ sourceImage = Image::FromStream(stream);
        try
        {
            return gcnew Bitmap(sourceImage);
        }
        finally
        {
            delete sourceImage;
        }
    }
    finally
    {
        delete stream;
    }
}

void AppForm::Searching(String^ Path)
{
    StateUpdate(AppState::Wait);
    StatusUpdate("Searching UPlugins ...");
    ControlsStateChange(ControlsState::Wait);
    ReleasePluginIcons();
    AppForm::dtbPlugins->Clear();
    AppForm::dtbPluginsOrig->Clear();
    AppForm::grdPlugins->DataSource = nullptr;

    if (!Path->Contains("Plugins"))
    {
        Path = Append(Path, "\\Engine\\Plugins");
    }

    FindUPlugin(Path);

    AppForm::dtbPlugins->DefaultView->Sort = "celEnabledByDefault DESC, celFriendlyName ASC, celCategory ASC";
    AppForm::dtbPlugins->AcceptChanges();
    AppForm::dtbPluginsOrig = AppForm::dtbPlugins->Copy();
    AppForm::dtbPluginsOrig->DefaultView->Sort = "celEnabledByDefault DESC, celFriendlyName ASC, celCategory ASC";
    AppForm::grdPlugins->DataSource = AppForm::dtbPlugins;

    if (AppForm::grdPlugins->Rows->Count > 0)
    {
        AppForm::grdPlugins->CurrentCell = AppForm::grdPlugins[0, 0];
    }

    ControlsStateChange(ControlsState::Default);
    StatusUpdate(Append(Append(Append(CountEnabledByDefault().ToString(), " plugins Enabled By Default. "), AppForm::dtbPlugins->Rows->Count.ToString()), " plugins total."));
    StateUpdate(AppState::Default);
}

void AppForm::FindUPlugin(String^ Path)
{
    for each (String^ dirPlugin in Directory::EnumerateDirectories(Path) )
    {
        if ( IsIgnoredFolder(dirPlugin) ) 
        {
            continue;
        }

        StatusUpdate(Append("Searching UPlugins :", dirPlugin));
        Application::DoEvents();

        for each (String^ mFile in Directory::GetFiles( dirPlugin, "*.uplugin" ) )
        {
            AddUPlugin(mFile);
            break;
        }
        if ( Directory::GetDirectories(dirPlugin)->Length != 0 )
        {
            FindUPlugin(dirPlugin);
        }
    }
}

void AppForm::AddUPlugin(String^ FileUPlugin)
{
    DataRow^ mDR = AppForm::dtbPlugins->NewRow();
    ReadUPlugin(FileUPlugin, mDR);
    if (!AppForm::dtbPlugins->Rows->Contains(mDR["celName"])) {
      AppForm::dtbPlugins->Rows->Add(mDR);
    } else {
      StatusUpdate(Append("Duplicated key : ", mDR["celName"]->ToString()));
    }
}

void AppForm::ReadUPlugin(String^ FileUPlugin, DataRow^& mDataRow)
{
    CheckAcess(FileUPlugin);
    FileStream^ filestream = gcnew FileStream(FileUPlugin, FileMode::Open);
    StreamReader^ reader = gcnew StreamReader(filestream);

    mDataRow["celName"] = Path::GetFileNameWithoutExtension(FileUPlugin);
    mDataRow["celPath"] = FileUPlugin->Substring(FileUPlugin->IndexOf("plugins", StringComparison::CurrentCultureIgnoreCase) + 8);

    Collections::Generic::List<String^>^ aDependencies = gcnew Collections::Generic::List<String^>();
    bool bReadingDependencies = false;

    try {
        String^ pluginIconPath = Append(Path::GetDirectoryName(FileUPlugin), "\\Resources\\icon128.png");
        String^ defaultIconPath = Append(EngineRootPath, "\\Engine\\Plugins\\Editor\\PluginBrowser\\Resources\\DefaultIcon128.png");

        Image^ iconImage = nullptr;
        if (File::Exists(pluginIconPath))
        {
            iconImage = LoadImageUnlocked(pluginIconPath);
        }
        else
        {
            iconImage = LoadImageUnlocked(defaultIconPath);
        }

        if (iconImage != nullptr)
        {
            mDataRow["celIcon"] = iconImage;
        }
    }
    catch (...) {}

    while ( !reader->EndOfStream )
    {
        String^ line = reader->ReadLine();
        String^ trimmedLine = line->Trim();

        if (trimmedLine->StartsWith("\"Plugins\"", StringComparison::CurrentCultureIgnoreCase))
        {
            bReadingDependencies = !trimmedLine->Contains("]");
            continue;
        }

        if (bReadingDependencies)
        {
            if (trimmedLine->Contains("\"Name\""))
            {
                String^ dependencyName = line;
                GetJSONValue(dependencyName);
                aDependencies->Add(dependencyName);
            }
            if (trimmedLine->Contains("]"))
            {
                bReadingDependencies = false;
            }
            continue;
        }

        if ( line->Contains("VersionName") ) 
        {
            GetJSONValue(line);
            mDataRow["celVersionName"] = line;
            continue;            
        }
        if (line->Contains("Installed"))
        {
          GetJSONValue(line);
          mDataRow["celInstalled"] = SafeToBoolean(line);
          continue;
        }
        if ( line->Contains("EnabledByDefault") ) 
        {
            GetJSONValue(line);
            mDataRow["celEnabledByDefault"] = SafeToBoolean(line);
            continue;            
        }
        if ( line->Contains("FriendlyName") ) 
        {
            GetJSONValue(line);
            mDataRow["celFriendlyName"] = line;
            continue;            
        }
        if ( line->Contains("Description") ) 
        {
            GetJSONValue(line);
            mDataRow["celDescription"] = line;
            continue;            
        }
        if ( line->Contains("Category") ) 
        {
            GetJSONValue(line);
            mDataRow["celCategory"] = line;
            continue;            
        }
    }
    mDataRow["celDependencies"] = String::Join(", ", aDependencies->ToArray());

    reader->Close();
    filestream->Close();
};

bool SafeToBoolean(String^ input) {
    if (String::IsNullOrWhiteSpace(input)) {
        return false;
    }
    
    String^ trimmed = input->Trim();
    if (String::Compare(trimmed, "1", true) == 0 || 
        String::Compare(trimmed, "true", true) == 0 ||
        String::Compare(trimmed, "yes", true) == 0 ||
        String::Compare(trimmed, "on", true) == 0) {
        return true;
    }
    if (String::Compare(trimmed, "0", true) == 0 || 
        String::Compare(trimmed, "false", true) == 0 ||
        String::Compare(trimmed, "no", true) == 0 ||
        String::Compare(trimmed, "off", true) == 0) {
        return false;
    }
    
    try {
        return System::Convert::ToBoolean(trimmed);
    } catch (FormatException^) {
        return false;
    }
}

void GetJSONValue(String^& str0)
{
    if ( str0->Contains(": \"") )
    {
        str0 = str0->Substring(str0->IndexOf(": \"") + 3);
        str0 = str0->Substring(0, str0->Length - 2);
    } 
    else 
    {
        str0 = str0->Substring(str0->IndexOf(": ") + 2);
    }
    if (str0->Length > 0)
    {
      str0 = str0->Trim();
      if (str0->Substring(str0->Length - 1, 1) == ",") 
      {
          str0 = str0->Substring(0, str0->Length - 1);
      }
    }
}

int AppForm::CountEnabledByDefault()
{
    int iCounted = 0;
    for each ( DataRow^ mDR in AppForm::dtbPlugins->Rows )
    {
        if ((bool)mDR["celEnabledByDefault"] == true) 
        {
            iCounted++;
        }
    }
    return iCounted;
}

bool IsIgnoredFolder(String^ Path)
{
    bool bIsIgnoreFolder = false;
    array<String^>^ aIgnoreFolders = {"Binaries", "Config", "Content", "Docs", "Intermediate", "Library", "Private", "Public", "Resources", "SDK", "SDKs", "Shaders", "Source", "SourceArt", "ThirdParty"};
    for each (String^ sIgnoreFolder in aIgnoreFolders) 
    {
        if (sIgnoreFolder->ToLower() == Path->Substring(System::IO::Path::GetDirectoryName(Path)->Length + 1)->ToLower() ) 
        {
            bIsIgnoreFolder = true;
            break;
        }
    }
    return bIsIgnoreFolder;
}

void AppForm::ControlsStateChange(ControlsState State)
{
    bool bState = (bool)State;
    AppForm::cmbUEFolder->Enabled = bState;
    AppForm::btnBrowse->Enabled = bState;
    AppForm::mnuMain->Enabled = bState;
    AppForm::btnSave->Enabled = bState;
    AppForm::grdPlugins->Enabled = bState;
  
    switch ( bState ) {
        case false:
            AppForm::btnSave->BackColor = Color::Empty;
            break;
        case true:
            AppForm::btnSave->BackColor = Color::LightGreen;
           break;
    }
}

void AppForm::txtSearch_GotFocus(System::Object^ sender, System::EventArgs^ e)
{
  txtSearch->SelectAll();
}

void AppForm::txtSearch_KeyUp(System::Object^ sender, System::Windows::Forms::KeyEventArgs^ e)
{
  AppForm::dtbPlugins->DefaultView->RowFilter = String::Format("celFriendlyName LIKE '%{0}%'", txtSearch->Text);

  if (e->Control && e->KeyCode == Keys::A) {
    if (sender != nullptr)
      ((TextBox^)sender)->SelectAll();
  }
}

void AppForm::BackupAll()
{
    String^ bkpPath = Append(Application::StartupPath, "\\UEPlugins_DisableDefault.uplugins_backup");
    if (!File::Exists(bkpPath))
    {
        StateUpdate(AppState::Wait);
        StatusUpdate("Generating backup before first use...");
        ControlsStateChange(ControlsState::Wait);

        FileStream^ newStream = gcnew FileStream(bkpPath, FileMode::CreateNew);
        StreamWriter^ newWriter = gcnew StreamWriter(newStream);

        Collections::Generic::List<String^>^ aPlugins = FindAllUPlugins(Append(EngineRootPath, "\\Engine\\Plugins"));
        for each (String ^ sPlugin in aPlugins)
        {
            for each (String ^ sLine in File::ReadLines(sPlugin))
            {
                if (sLine->Contains("EnabledByDefault") && sLine->Contains("true"))
                {
                    newWriter->WriteLine(sPlugin);
                }
            }
        }

        newWriter->Close();
        newStream->Close();
        ControlsStateChange(ControlsState::Default);
        StatusUpdate(Append("Backup created at ", bkpPath));
        StateUpdate(AppState::Default);
    }
    else
    {
        StatusUpdate("");
    }
}

List<String^>^ AppForm::FindAllUPlugins(String^ Path)
{
  Generic::List<String^>^ aPluginsList = gcnew Generic::List<String^>();
  for each (String ^ dirPlugin in Directory::EnumerateDirectories(Path)) {
    if (IsIgnoredFolder(dirPlugin)) {
      continue;
    }
    for each (String ^ mFile in Directory::GetFiles(dirPlugin, "*.uplugin")) {
      aPluginsList->Add(mFile);
      break;
    }
    if (Directory::GetDirectories(dirPlugin)->Length != 0) {
      Generic::List<String^>^ aPluginsSubList = FindAllUPlugins(dirPlugin);
      for each (String^ SubList in aPluginsSubList) {
        aPluginsList->Add(SubList);
      }
    }
  }
  return aPluginsList;
}

void CheckAcess(String^ FileUPlugin)
{
  FileAttributes oFileAttrib = File::GetAttributes(FileUPlugin);
  if ((oFileAttrib & FileAttributes::ReadOnly) == FileAttributes::ReadOnly) {
    try
    {
      File::SetAttributes(FileUPlugin, oFileAttrib ^ FileAttributes::ReadOnly);
    }
    catch (const Exception^ err)
    {
      const Exception^ _ = err;
      throw gcnew AccessViolationException(Append(Append("File Access Error: ", FileUPlugin), " is Read-Only, try to run UEPlugins_DisableDefault as Administrator."));
    }
  }
}

void CopyDirectoryRecursive(String^ SourcePath, String^ TargetPath)
{
    Directory::CreateDirectory(TargetPath);

    for each (String ^ filePath in Directory::GetFiles(SourcePath))
    {
        String^ targetFilePath = Append(Append(TargetPath, "\\"), Path::GetFileName(filePath));
        File::Copy(filePath, targetFilePath, true);
    }

    for each (String ^ directoryPath in Directory::GetDirectories(SourcePath))
    {
        String^ targetDirectoryPath = Append(Append(TargetPath, "\\"), Path::GetFileName(directoryPath));
        CopyDirectoryRecursive(directoryPath, targetDirectoryPath);
    }
}

void DeleteDirectoryRecursive(String^ Path)
{
    if (Directory::Exists(Path))
    {
        Directory::Delete(Path, true);
    }
}

void MoveDirectorySafe(String^ SourcePath, String^ TargetPath)
{
    if (Directory::Exists(TargetPath))
    {
        DeleteDirectoryRecursive(TargetPath);
    }

    try
    {
        Directory::Move(SourcePath, TargetPath);
    }
    catch (IOException^)
    {
        CopyDirectoryRecursive(SourcePath, TargetPath);
        DeleteDirectoryRecursive(SourcePath);
    }
    catch (UnauthorizedAccessException^)
    {
        CopyDirectoryRecursive(SourcePath, TargetPath);
        DeleteDirectoryRecursive(SourcePath);
    }
}

void AppForm::mnuBackupSave_Click(System::Object^ sender, System::EventArgs^ e) 
{
  bool bIsTemplate = false;
  bIsTemplate = sender->ToString()->Contains("template");

  String^ bkpPath;
  if (!bIsTemplate) {
    bkpPath = Append(Application::StartupPath, "\\*.uplugins_backup");
  } else {
    bkpPath = Append(Application::StartupPath, "\\*.uplugins_template");
  }

  SaveFileDialog^ dlgBkpSave = gcnew SaveFileDialog();
  dlgBkpSave->AddExtension = true;
  dlgBkpSave->CheckPathExists = true;
  if (!bIsTemplate) {
    dlgBkpSave->FileName = "MyBackup_01";
    dlgBkpSave->DefaultExt = ".uplugins_backup";
    dlgBkpSave->Filter = "Default|*.uplugins_backup";
    dlgBkpSave->Title = "Save uplugins_backup";
  } else {
    dlgBkpSave->FileName = "MyTemplate_01";
    dlgBkpSave->DefaultExt = ".uplugins_template";
    dlgBkpSave->Filter = "Default|*.uplugins_template";
    dlgBkpSave->Title = "Save uplugins_template";
  }
  dlgBkpSave->InitialDirectory = Application::StartupPath;
  dlgBkpSave->OverwritePrompt = true;
  if (dlgBkpSave->ShowDialog() == Windows::Forms::DialogResult::OK) {
    bkpPath = dlgBkpSave->FileName;
    if (bkpPath->Contains("UEPlugins_DisableDefault.uplugins_backup")) {
      MessageBox::Show(this, 
        "Please, don't use UEPlugins_DisableDefault.uplugins_backup name, this is your main backup for all Unreal Engine folders. Try again with a different filename.", 
        "UEPlugins_DisableDefault", 
        MessageBoxButtons::OK, 
        MessageBoxIcon::Information);
      return;
    }

    StateUpdate(AppState::Wait);
    if (!bIsTemplate) {
      StatusUpdate("Saving backup...");
    } else {
      StatusUpdate("Saving template...");
    }
    ControlsStateChange(ControlsState::Wait);

    if (File::Exists(bkpPath)) File::Delete(bkpPath);
    FileStream^ newStream = gcnew FileStream(bkpPath, FileMode::CreateNew);
    StreamWriter^ newWriter = gcnew StreamWriter(newStream);
    for each (DataRow ^ mDRPlug in dtbPlugins->Rows) {
      String^ UEPath = Append(AppForm::EngineRootPath, "\\Engine\\Plugins\\");
      if (mDRPlug["celEnabledByDefault"]->ToString()->ToLower() == "true") {
        if (!bIsTemplate) {
          newWriter->WriteLine(Append(UEPath, mDRPlug["celPath"]->ToString()));
        } else {
          newWriter->WriteLine(mDRPlug["celPath"]->ToString());
        }
      }
    }
    newWriter->Close();
    newStream->Close();

    ControlsStateChange(ControlsState::Default);
    if (!bIsTemplate) {
      StatusUpdate(Append("Backup created at ", bkpPath));
    } else {
      StatusUpdate(Append("Template created at ", bkpPath));
    }
    StateUpdate(AppState::Default);
  }
}

void AppForm::mnuBackupLoad_Click(System::Object^ sender, System::EventArgs^ e)
{
  bool bIsTemplate = false;
  bIsTemplate = sender->ToString()->Contains("template");

  String^ bkpPath;
  if (!bIsTemplate) {
    bkpPath = Append(Application::StartupPath, "\\*.uplugins_backup");
  } else {
    bkpPath = Append(Application::StartupPath, "\\*.uplugins_template");
  }

  OpenFileDialog^ dlgBkpSave = gcnew OpenFileDialog();
  dlgBkpSave->AddExtension = true;
  dlgBkpSave->CheckFileExists = true;
  dlgBkpSave->CheckPathExists = true;

  if (!bIsTemplate) {
    dlgBkpSave->FileName = "MyBackup_01";
    dlgBkpSave->DefaultExt = ".uplugins_backup";
    dlgBkpSave->Filter = "Default|*.uplugins_backup";
    dlgBkpSave->Title = "Load uplugins_backup";
  } else {
    dlgBkpSave->FileName = "MyTemplate_01";
    dlgBkpSave->DefaultExt = ".uplugins_template";
    dlgBkpSave->Filter = "Default|*.uplugins_template";
    dlgBkpSave->Title = "Load uplugins_template";
  }

  dlgBkpSave->InitialDirectory = Application::StartupPath;
  if (dlgBkpSave->ShowDialog() == Windows::Forms::DialogResult::OK) {
    bkpPath = dlgBkpSave->FileName;

    StateUpdate(AppState::Wait);
    if (!bIsTemplate) {
      StatusUpdate("Loading backup...");
    } else {
      StatusUpdate("Loading template...");
    }
    ControlsStateChange(ControlsState::Wait);

    int Counter = 0;
    String^ UEPath = Append(AppForm::EngineRootPath, "\\Engine\\Plugins\\");
    FileStream^ newStream = gcnew FileStream(bkpPath, FileMode::Open);
    StreamReader^ newReader = gcnew StreamReader(newStream);
    for each (DataRow ^ mDRPlug in dtbPlugins->Rows) {
      mDRPlug["celEnabledByDefault"] = false;
    }
    while (!newReader->EndOfStream)
    {
      String^ sLine = newReader->ReadLine();
      if (!bIsTemplate) {
        if (sLine->Contains(UEPath)) {
          for each (DataRow ^ mDRPlug in dtbPlugins->Rows) {
            if (sLine->Contains(mDRPlug["celPath"]->ToString())) {
              mDRPlug["celEnabledByDefault"] = true;
              Counter++;
            }
          }
        }
      } else {
        for each (DataRow ^ mDRPlug in dtbPlugins->Rows) {
          if (sLine->Contains(mDRPlug["celPath"]->ToString())) {
            mDRPlug["celEnabledByDefault"] = true;
            Counter++;
          }
        }
      }
    }
    newReader->Close();
    newStream->Close();

    ControlsStateChange(ControlsState::Default);
    if (!bIsTemplate) {
      StatusUpdate(Append(Append("Backup loaded. ", Counter.ToString()), " plugins EnabledByDefault."));
    } else {
      StatusUpdate(Append(Append("Template loaded. ", Counter.ToString()), " plugins EnabledByDefault."));
    }
    StateUpdate(AppState::Default);
  }
}


void AppForm::mnuTemplateSave_Click(System::Object^ sender, System::EventArgs^ e)
{
  mnuBackupSave_Click(sender, e);
}

void AppForm::mnuTemplateLoad_Click(System::Object^ sender, System::EventArgs^ e)
{
  mnuBackupLoad_Click(sender, e);
}
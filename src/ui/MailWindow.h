#pragma once
#include "mail/Store.h"
#include "Credentials.h"
#include <Window.h>
#include <atomic>
#include <memory>
#include <thread>
#include <map>
#include <set>
#include <Messenger.h>

class BListView;
class BOutlineListView;
class BTextControl;
class BTextView;
class BStringView;
class BStatusBar;
class BFilePanel;
class BMessageRunner;
class BButton;
class BSplitView;
class BView;

namespace mail::ui {
class ReaderView;
class MailboxItem;
class MailWindow : public BWindow {
public:
    explicit MailWindow(std::shared_ptr<Store> store,GoogleOptions google={},std::string database={});
    ~MailWindow() override;
    void MessageReceived(BMessage*) override;
    bool QuitRequested() override;
private:
    void BuildToolbar(BView* toolbar);
    void BuildSidebar();
    void RememberExpansion();
    void UpdateCounts();
    void OpenOrganization(int kind,bool create=false);
    void RefreshOrganization();
    void Refresh();
    void ShowConversation();
    void UpdateActions();
    void OpenFormatted(Id thread=0,Id message=0);
    void OpenLink(const std::string& url);
    void Import(BMessage*);
    void Apply(uint32 action);
    bool ConfirmDelete(const std::vector<Id>& messages,bool whole);
    void EmptyTrash();
    void CheckMail(bool draftsOnly=false);
    void ShowSyncProgress(BMessage*);
    void SetBusy(bool busy);
    void Compose(uint32 action,Id draft=0,const std::string& to={},const std::string& subject={});
    void SendQueued();
    void RefreshOutbox();
    void RefreshReminders(bool notify=false);
    void OpenSnooze(const std::vector<Id>&);
    void OpenReminder(Id,bool again);
    void SelectView(const std::string& key);
    void LoadSettings();
    void SaveSettings();
    std::vector<Id> SelectedMessages();
    void Error(const std::string&);
    std::shared_ptr<Store> fStore;
    GoogleOptions fGoogle;
    BOutlineListView* fSidebar = nullptr;
    BListView* fConversations = nullptr;
    BListView* fAttachments = nullptr;
    BView* fAttachmentBox = nullptr;
    BTextControl* fSearch = nullptr;
    ReaderView* fReader = nullptr;
    BStringView* fStatus = nullptr;
    BStatusBar* fProgress = nullptr;
    BStringView* fTitle = nullptr;
    BStringView* fListTitle = nullptr;
    BSplitView* fSplit = nullptr;
    BButton* fCheckMail = nullptr;
    BButton* fOutboxButton = nullptr;
    BButton* fMoveButton = nullptr;
    BButton* fReminderButton = nullptr;
    BButton *fAttachmentPreview=nullptr,*fAttachmentSave=nullptr;
    BButton* fFormattedView=nullptr;
    BButton* fPlainButton=nullptr;
    BMenuItem* fTrashItem=nullptr;
    std::map<uint32,BButton*> fActions;
    std::map<std::string,Id> fFormattedThreads;
    BMessenger fOutbox;
    BMessenger fMoves;
    BMessenger fDraftReview;
    BMessenger fSnooze,fReminders;
    std::string fDatabase;
    std::map<int,BMessenger> fOrganization;
    std::unique_ptr<BFilePanel> fImportPanel;
    std::unique_ptr<BFilePanel> fSavePanel;
    std::unique_ptr<BMessageRunner> fTimer;
    std::unique_ptr<BMessageRunner> fDraftTimer;
    std::unique_ptr<BMessageRunner> fReadTimer;
    std::vector<Query> fViews;
    std::vector<std::string> fViewKeys;
    std::vector<MailboxItem*> fViewItems;
    std::vector<Attachment> fFiles;
    Attachment fSaving;
    Query fQuery;
    std::string fViewName;
    std::thread fImportWorker;
    std::thread fSyncWorker;
    std::thread fSendWorker;
    std::atomic<bool> fCancel{false};
    std::atomic<bool> fCancelSync{false};
    std::atomic<bool> fCancelSend{false};
    bool fImporting = false;
    bool fSyncing = false;
    bool fAccountsOpen = false;
    bool fSending = false;
    bool fNeedsSentCheck = false;
    std::map<Id,BMessenger> fComposers;
    std::string fSyncError;
    Timestamp fLastSync = 0;
    Timestamp fDraftDue = 0;
    Id fLocalAccount = 0;
    Id fShownThread = 0;
    std::string fShownSignature;
    std::set<std::string> fCollapsed;
};
}

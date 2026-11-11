#include "MailWindow.h"
#include "SnoozeWindow.h"
#include "ReminderWindow.h"
#include "mail/Snooze.h"
#include "Emoji.h"
#include "Icons.h"
#include "AccountWindow.h"
#include "Credentials.h"
#include "ComposerWindow.h"
#include "OutboxWindow.h"
#include "MoveWindow.h"
#include "DraftWindow.h"
#include "OrganizationWindow.h"
#include "AttachmentViews.h"
#include "PreviewTransfer.h"
#include "ReaderView.h"
#include "mail/Outbox.h"
#include "mail/Mime.h"
#include "mail/Sync.h"
#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <CheckBox.h>
#include <Entry.h>
#include <File.h>
#include <FilePanel.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MimeType.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <OutlineListView.h>
#include <Path.h>
#include <Screen.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <SplitView.h>
#include <StatusBar.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <Url.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <set>
#include <stdexcept>
#include <strings.h>

namespace mail::ui {
namespace {
enum : uint32 {
    kImport='mimp', kImported='mimD', kView='mviw', kSelect='msel', kSearch='msch',
    kArchive='marc', kTrash='mtrs', kInbox='minb', kRead='mred', kUnread='munr',
    kStar='mstr', kSnooze='msnz',kQuickSnooze='ms1h', kWake='mwak', kTick='mtik', kTag='mtag',
    kSaveSmart='msmt', kSaveFilter='mflt', kEditDone='medt', kSaveAttachment='matt', kAttachmentSaved='masv',
    kAccounts='macc', kCheckMail='mchk', kSyncProgress='msyp', kSyncDone='msyd',kDraftTick='mdtk',
    kCompose='mnew',kReply='mrpl',kReplyAll='mrpa',kForward='mfwd',kOutbox='mout',kSendDone='msdd',
    kAttachmentSelected='masl',kPreviewAttachment='mapr',kManageRules='mmrl',kManageSmart='mmsf',kManageTags='mmtg',kRemoveTag='mrtg',kFormatted='mfmt',
    kToggleRead='mtrd',kPlainText='mplt',kMarkShownRead='mrdt',kMakeDefault='mdef',
    kDeleteForever='mdel',kEmptyTrash='memt'
};
Timestamp now() { return static_cast<Timestamp>(std::time(nullptr)); }
void clear(BListView* list) { while (auto* item=list->RemoveItem(int32(0))) delete item; }
std::string date(Timestamp time)
{
    std::time_t value=static_cast<std::time_t>(time); std::tm tm{}; localtime_r(&value,&tm);
    char text[80]; std::strftime(text,sizeof(text),"%d %b %Y, %H:%M",&tm); return text;
}
// Today's mail shows its time, this week's its weekday, older mail its date.
std::string shortDate(Timestamp time)
{
    std::time_t value=static_cast<std::time_t>(time),current=std::time(nullptr); std::tm tm{},today{};
    localtime_r(&value,&tm); localtime_r(&current,&today);
    char text[40];
    if (tm.tm_year==today.tm_year && tm.tm_yday==today.tm_yday) std::strftime(text,sizeof(text),"%H:%M",&tm);
    else if (value<=current && current-value<6*86400) std::strftime(text,sizeof(text),"%a %H:%M",&tm);
    else if (tm.tm_year==today.tm_year) std::strftime(text,sizeof(text),"%d %b",&tm);
    else std::strftime(text,sizeof(text),"%d %b %Y",&tm);
    return text;
}
std::string singleLine(std::string s)
{
    for (char& c : s) if (c=='\r' || c=='\n' || c=='\t') c=' ';
    return s;
}
// The display name of an address such as "Ann Example <ann@example.test>".
std::string senderName(const std::string& sender)
{
    auto open=sender.find('<');
    std::string name=open==std::string::npos?sender:sender.substr(0,open);
    while (!name.empty() && (name.back()==' ' || name.back()=='"')) name.pop_back();
    while (!name.empty() && (name.front()==' ' || name.front()=='"')) name.erase(0,1);
    return name.empty()?sender:name;
}
float baseline(BRect frame,const BFont& font)
{
    font_height height; font.GetHeight(&height);
    return std::floor(frame.top+(frame.Height()-(height.ascent+height.descent))/2+height.ascent);
}
}

// A mailbox, account or section in the sidebar. Its text stays the plain
// mailbox name; the icon and unread count are drawn beside it.
class MailboxItem : public BStringItem {
public:
    MailboxItem(const char* text,const char* icon,int32 view,uint32 level=0,bool header=false)
        :BStringItem(text,level),fIcon(icon),fView(view),fHeader(header) {}
    void Update(BView* owner,const BFont* font) override
    {
        BStringItem::Update(owner,font);
        font_height height; font->GetHeight(&height);
        SetHeight(std::ceil(std::max(height.ascent+height.descent+height.leading,IconSize())+(fHeader?10:6)));
    }
    void DrawItem(BView* owner,BRect bounds,bool) override
    {
        bool selected=IsSelected() && !fHeader;
        auto background=ui_color(selected?B_LIST_SELECTED_BACKGROUND_COLOR:B_LIST_BACKGROUND_COLOR);
        auto text=ui_color(selected?B_LIST_SELECTED_ITEM_TEXT_COLOR:B_LIST_ITEM_TEXT_COLOR);
        owner->SetHighColor(background); owner->FillRect(bounds); owner->SetLowColor(background);
        BFont font; owner->GetFont(&font); BFont label=font;
        float x=bounds.left+4;
        if (fHeader) {
            label.SetFace(B_BOLD_FACE); label.SetSize(font.Size()*0.85f);
            owner->SetHighColor(tint_color(text,B_LIGHTEN_1_TINT));
            DrawEmojiText(*owner,Text(),label,BPoint(x,bounds.bottom-5),bounds.Width()-8);
            return;
        }
        if (auto* icon=fIcon?MailIcon(fIcon):nullptr) {
            owner->SetDrawingMode(B_OP_ALPHA);
            owner->DrawBitmap(icon,BPoint(x,std::floor(bounds.top+(bounds.Height()-icon->Bounds().Height())/2)));
            owner->SetDrawingMode(B_OP_COPY);
            x+=icon->Bounds().Width()+6;
        }
        float right=bounds.right-6;
        if (fCount) {
            BFont bold=font; bold.SetFace(B_BOLD_FACE);
            auto count=std::to_string(fCount); float width=bold.StringWidth(count.c_str());
            owner->SetHighColor(selected?text:ui_color(B_CONTROL_HIGHLIGHT_COLOR)); owner->SetFont(&bold);
            owner->DrawString(count.c_str(),BPoint(right-width,baseline(bounds,bold)));
            owner->SetFont(&font); right-=width+8;
            label.SetFace(B_BOLD_FACE);
        }
        owner->SetHighColor(text);
        DrawEmojiText(*owner,singleLine(Text()),label,BPoint(x,baseline(bounds,label)),right-x);
    }
    void SetCount(std::size_t count) { fCount=count; }
    std::size_t Count() const { return fCount; }
    int32 View() const { return fView; }
    bool Header() const { return fHeader; }
private:
    const char* fIcon;
    int32 fView;
    bool fHeader;
    std::size_t fCount=0;
};
namespace {
class ConversationItem : public BListItem {
public:
    explicit ConversationItem(Conversation value) : conversation(std::move(value)) {}
    void Update(BView* owner,const BFont* font) override
    {
        BListItem::Update(owner,font);
        font_height height; font->GetHeight(&height);
        SetHeight(std::ceil((height.ascent+height.descent+height.leading)*3+font->Size()*1.1f)); SetWidth(200);
    }
    void DrawItem(BView* owner,BRect frame,bool) override
    {
        auto background=ui_color(IsSelected()?B_LIST_SELECTED_BACKGROUND_COLOR:B_LIST_BACKGROUND_COLOR);
        auto foreground=ui_color(IsSelected()?B_LIST_SELECTED_ITEM_TEXT_COLOR:B_LIST_ITEM_TEXT_COLOR);
        auto muted=IsSelected()?foreground:tint_color(foreground,foreground.IsLight()?B_DARKEN_2_TINT:B_LIGHTEN_1_TINT);
        owner->SetHighColor(background); owner->FillRect(frame); owner->SetLowColor(background);
        BFont plain; owner->GetFont(&plain); BFont strong=plain; strong.SetFace(B_BOLD_FACE);
        font_height height; plain.GetHeight(&height);
        float line=height.ascent+height.descent+height.leading;
        float left=frame.left+plain.Size()*1.4f,right=frame.right-plain.Size()*0.7f;
        float y=frame.top+plain.Size()*0.55f+height.ascent;
        bool unread=conversation.unread>0;
        if (unread) {
            // An unread marker in the margin, as in most mail readers.
            float radius=std::max(2.5f,plain.Size()*0.25f);
            owner->SetHighColor(IsSelected()?foreground:ui_color(B_CONTROL_HIGHLIGHT_COLOR));
            owner->FillEllipse(BPoint(frame.left+plain.Size()*0.7f,y-height.ascent/2+1),radius,radius);
        }
        auto when=shortDate(conversation.latest); float whenWidth=plain.StringWidth(when.c_str());
        owner->SetHighColor(unread?foreground:muted); owner->SetFont(unread?&strong:&plain);
        owner->DrawString(when.c_str(),BPoint(right-whenWidth,y)); owner->SetFont(&plain);
        std::string sender=conversation.id<0?"Draft":senderName(conversation.sender);
        if (conversation.count>1) sender+="  ("+std::to_string(conversation.count)+")";
        owner->SetHighColor(conversation.id<0?ui_color(B_FAILURE_COLOR):foreground);
        DrawEmojiText(*owner,singleLine(sender),unread?strong:plain,BPoint(left,y),right-left-whenWidth-10);
        y+=line;
        // Small markers to the right of the subject.
        float markers=right;
        auto marker=[&](const char* name) {
            if (auto* icon=MailIcon(name)) {
                markers-=icon->Bounds().Width()+1;
                owner->SetDrawingMode(B_OP_ALPHA); owner->DrawBitmap(icon,BPoint(markers,y-height.ascent-1)); owner->SetDrawingMode(B_OP_COPY);
                markers-=3;
            }
        };
        if (conversation.starred) marker("star");
        if (conversation.attachments) marker("attachment");
        owner->SetHighColor(foreground);
        DrawEmojiText(*owner,singleLine(conversation.subject.empty()?"(No subject)":conversation.subject),unread?strong:plain,BPoint(left,y),markers-left-4);
        y+=line;
        owner->SetHighColor(muted);
        DrawEmojiText(*owner,singleLine(conversation.preview),plain,BPoint(left,y),right-left);
        owner->SetHighColor(tint_color(ui_color(B_LIST_BACKGROUND_COLOR),B_DARKEN_1_TINT));
        owner->StrokeLine(BPoint(frame.left,frame.bottom),BPoint(frame.right,frame.bottom));
        owner->SetFont(&plain);
    }
    Conversation conversation;
};
// Delete moves the selected conversations to Trash, as in other mail readers;
// in Trash it deletes them for good (see MailWindow::Apply).
class ConversationListView : public BListView {
public:
    ConversationListView() : BListView("conversations",B_MULTIPLE_SELECTION_LIST) {}
    void KeyDown(const char* bytes,int32 count) override
    {
        if (count==1 && bytes[0]==B_DELETE && Window()) { Window()->PostMessage(kTrash); return; }
        BListView::KeyDown(bytes,count);
    }
};
// Small native forms share the same commit/cancel behavior and send results
// to the mailbox looper rather than retaining a raw owner pointer.
class EditWindow : public BWindow {
public:
    EditWindow(BWindow* owner,uint32 action,const char* title,const char* prompt)
        : BWindow(BRect(0,0,440,180),title,B_TITLED_WINDOW_LOOK,B_MODAL_SUBSET_WINDOW_FEEL,
            B_AUTO_UPDATE_SIZE_LIMITS|B_CLOSE_ON_ESCAPE),fOwner(owner),fAction(action)
    {
        AddToSubset(owner);
        fName=new BTextControl("name",prompt,"",nullptr);
        fName->SetModificationMessage(new BMessage('etxt'));
        auto* save=new BButton(action==kRemoveTag?"Remove tag":"Add tag",new BMessage(kEditDone));
        BLayoutBuilder::Group<>(this,B_VERTICAL).SetInsets(B_USE_WINDOW_INSETS)
            .Add(fName)
            .AddGroup(B_HORIZONTAL).AddGlue().Add(new BButton("Cancel",new BMessage(B_QUIT_REQUESTED))).Add(save).End();
        SetDefaultButton(save); CenterIn(owner->Frame()); fName->MakeFocus();
    }
    void MessageReceived(BMessage* message) override
    {
        if (message->what=='etxt') { ApplyEmojiFonts(*fName->TextView()); return; }
        if (message->what==kEditDone) {
            if (fName->Text()[0]=='\0') { fName->MakeFocus(); return; }
            BMessage reply(fAction); reply.AddString("value",fName->Text()); fOwner.SendMessage(&reply); Quit(); return;
        }
        BWindow::MessageReceived(message);
    }
private:
    BMessenger fOwner; uint32 fAction; BTextControl* fName;
};
}

MailWindow::MailWindow(std::shared_ptr<Store> store,GoogleOptions google,std::string database)
    : BWindow(BRect(70,60,1250,820),"Mail",B_DOCUMENT_WINDOW,B_AUTO_UPDATE_SIZE_LIMITS|B_ASYNCHRONOUS_CONTROLS),fStore(std::move(store)),fGoogle(std::move(google)),fDatabase(std::move(database))
{
    for (const auto& a : fStore->accounts()) if (a.provider==Provider::Local) { fLocalAccount=a.id; break; }
    if (!fLocalAccount) { Account local; local.name="On this computer"; fLocalAccount=fStore->saveAccount(local); }
    fStore->recoverOutgoing();
    auto* menuBar=new BMenuBar("menu");
    auto* file=new BMenu("File");
    file->AddItem(new BMenuItem("New message",new BMessage(kCompose),'N'));
    file->AddItem(new BMenuItem("Check mail",new BMessage(kCheckMail),'M'));
    file->AddSeparatorItem();
    file->AddItem(new BMenuItem("Import mail…",new BMessage(kImport),'O'));
    file->AddSeparatorItem();
    file->AddItem(new BMenuItem("Outbox…",new BMessage(kOutbox)));
    file->AddItem(new BMenuItem("Reminders…",new BMessage(kReminders)));
    file->AddItem(new BMenuItem("Folder moves…",new BMessage(kFolderMoves)));
    file->AddItem(new BMenuItem("Draft synchronization…",new BMessage(kDraftReview)));
    file->AddSeparatorItem();
    file->AddItem(new BMenuItem("Accounts…",new BMessage(kAccounts),','));
    file->AddSeparatorItem(); file->AddItem(new BMenuItem("Quit",new BMessage(B_QUIT_REQUESTED),'Q')); menuBar->AddItem(file);
    auto* messages=new BMenu("Message");
    messages->AddItem(new BMenuItem("Reply",new BMessage(kReply),'R'));
    messages->AddItem(new BMenuItem("Reply all",new BMessage(kReplyAll),'R',B_COMMAND_KEY|B_SHIFT_KEY));
    messages->AddItem(new BMenuItem("Forward",new BMessage(kForward),'F',B_COMMAND_KEY|B_SHIFT_KEY));
    messages->AddSeparatorItem();
    messages->AddItem(new BMenuItem("Show plain text",new BMessage(kPlainText),'T',B_COMMAND_KEY|B_SHIFT_KEY));
    messages->AddItem(new BMenuItem("Open in new window",new BMessage(kFormatted),'P'));
    messages->AddSeparatorItem();
    messages->AddItem(new BMenuItem("Archive",new BMessage(kArchive),'E'));
    messages->AddItem(fTrashItem=new BMenuItem("Move to Trash",new BMessage(kTrash),'D'));
    messages->AddItem(new BMenuItem("Move to Inbox",new BMessage(kInbox)));
    messages->AddItem(new BMenuItem("Empty Trash…",new BMessage(kEmptyTrash)));
    messages->AddSeparatorItem();
    messages->AddItem(new BMenuItem("Mark as read",new BMessage(kRead),'K'));
    messages->AddItem(new BMenuItem("Mark as unread",new BMessage(kUnread),'U'));
    messages->AddItem(new BMenuItem("Toggle star",new BMessage(kStar),'L'));
    messages->AddItem(new BMenuItem("Add tag…",new BMessage(kTag),'T'));
    messages->AddItem(new BMenuItem("Remove tag…",new BMessage(kRemoveTag)));
    messages->AddSeparatorItem();
    messages->AddItem(new BMenuItem("Snooze…",new BMessage(kSnooze),'H'));
    messages->AddItem(new BMenuItem("Snooze for one hour",new BMessage(kQuickSnooze)));
    messages->AddItem(new BMenuItem("End snooze",new BMessage(kWake)));
    menuBar->AddItem(messages);
    auto* organize=new BMenu("Organize");
    organize->AddItem(new BMenuItem("Save view as smart folder…",new BMessage(kSaveSmart)));
    organize->AddItem(new BMenuItem("New rule…",new BMessage(kSaveFilter)));
    organize->AddSeparatorItem();
    organize->AddItem(new BMenuItem("Rules…",new BMessage(kManageRules)));
    organize->AddItem(new BMenuItem("Smart folders…",new BMessage(kManageSmart)));
    organize->AddItem(new BMenuItem("Tags…",new BMessage(kManageTags)));
    menuBar->AddItem(organize);

    fSidebar=new BOutlineListView("mailboxes"); fSidebar->SetSelectionMessage(new BMessage(kView));
    auto* sidebarScroll=new BScrollView("mailbox scroll",fSidebar,0,false,true,B_NO_BORDER);
    sidebarScroll->SetExplicitMinSize(BSize(150,200));
    fConversations=new ConversationListView();
    fConversations->SetSelectionMessage(new BMessage(kSelect));
    fConversations->SetInvocationMessage(new BMessage(kOpenDraft));
    auto* conversationScroll=new BScrollView("conversation scroll",fConversations,0,false,true,B_NO_BORDER);
    conversationScroll->SetExplicitMinSize(BSize(260,200));
    fListTitle=new BStringView("view title","");
    BFont heading(be_bold_font); fListTitle->SetFont(&heading);
    fListTitle->SetExplicitMinSize(BSize(50,B_SIZE_UNSET)); fListTitle->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,B_SIZE_UNSET));
    auto* list=new BGroupView(B_VERTICAL,0);
    BLayoutBuilder::Group<>(list).AddGroup(B_HORIZONTAL).SetInsets(10,6,10,6).Add(fListTitle).End()
        .Add(new BSeparatorView(B_HORIZONTAL)).Add(conversationScroll,1);
    fSearch=new BTextControl("search","Search:","",new BMessage(kSearch));
    fSearch->SetModificationMessage(new BMessage(kSearch));
    fSearch->SetExplicitMinSize(BSize(be_plain_font->StringWidth("Search:")+180,B_SIZE_UNSET));
    fSearch->SetToolTip("Search the current view by sender, subject or text.");
    fReader=new ReaderView();
    fTitle=new BStringView("subject","");
    BFont subject(be_bold_font); subject.SetSize(be_bold_font->Size()*1.25f); fTitle->SetFont(&subject);
    fTitle->SetExplicitMinSize(BSize(50,B_SIZE_UNSET));
    fTitle->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,B_SIZE_UNSET));
    fAttachments=new AttachmentListView("attachments",[this](int32 index)->const Attachment* {
        return index>=0 && static_cast<std::size_t>(index)<fFiles.size()?&fFiles[index]:nullptr;
    });
    fAttachments->SetInvocationMessage(new BMessage(kSaveAttachment)); fAttachments->SetSelectionMessage(new BMessage(kAttachmentSelected));
    auto* attachmentScroll=new BScrollView("attachments scroll",fAttachments,0,false,true);
    attachmentScroll->SetExplicitMinSize(BSize(100,55)); attachmentScroll->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,85));
    auto* attachmentTitle=new BStringView("attachment title","Attachments — drag to Tracker to save a copy");
    attachmentTitle->SetExplicitMinSize(BSize(80,B_SIZE_UNSET));
    attachmentTitle->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,B_SIZE_UNSET));
    fAttachmentPreview=new BButton("preview attachment","Preview",new BMessage(kPreviewAttachment)); fAttachmentPreview->SetEnabled(false);
    fAttachmentSave=new BButton("save attachment","Save…",new BMessage(kSaveAttachment)); fAttachmentSave->SetEnabled(false);
    fFormattedView=new BButton("formatted conversation","Open in window",new BMessage(kFormatted)); fFormattedView->SetEnabled(false);
    fFormattedView->SetToolTip("Open this conversation in its own window.");
    fPlainButton=new BButton("plain text","Plain text",new BMessage(kPlainText)); fPlainButton->SetEnabled(false);
    fPlainButton->SetToolTip("Switch between the formatted message and its plain text.");
    for (auto* button:{fFormattedView,fPlainButton}) button->SetFlat(true);
    if (auto* icon=MailIcon("attachment")) attachmentTitle->SetExplicitMinSize(BSize(icon->Bounds().Width()+80,B_SIZE_UNSET));
    fAttachmentBox=new BGroupView(B_VERTICAL,0);
    BLayoutBuilder::Group<>(static_cast<BGroupView*>(fAttachmentBox)).Add(new BSeparatorView(B_HORIZONTAL))
        .AddGroup(B_VERTICAL).SetInsets(12,6,12,10)
            .AddGroup(B_HORIZONTAL).Add(attachmentTitle,1).Add(fAttachmentPreview).Add(fAttachmentSave).End()
            .Add(attachmentScroll).End();
    auto* reader=new BGroupView(B_VERTICAL,0);
    BLayoutBuilder::Group<>(reader)
        .AddGroup(B_HORIZONTAL).SetInsets(14,8,8,8).Add(fTitle,1).Add(fPlainButton).Add(fFormattedView).End()
        .Add(new BSeparatorView(B_HORIZONTAL))
        .Add(fReader,1)
        .Add(fAttachmentBox);
    fSplit=new BSplitView(B_HORIZONTAL,0);
    fSplit->AddChild(sidebarScroll,0.17f); fSplit->AddChild(list,0.30f); fSplit->AddChild(reader,0.53f);
    fSplit->SetCollapsible(false);
    fStatus=new BStringView("status",""); fStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,B_SIZE_UNSET));
    fStatus->SetExplicitMinSize(BSize(50,B_SIZE_UNSET));
    fProgress=new BStatusBar("progress"); fProgress->SetBarHeight(be_plain_font->Size()*0.8f);
    fProgress->SetMaxValue(1000); fProgress->SetExplicitMinSize(BSize(260,B_SIZE_UNSET)); fProgress->SetExplicitMaxSize(BSize(420,B_SIZE_UNSET));
    fOutboxButton=new BButton("outbox","Outbox (0)",new BMessage(kOutbox));
    fMoveButton=new BButton("folder moves","Folder moves (0)",new BMessage(kFolderMoves));
    fReminderButton=new BButton("reminders","Reminders (0)",new BMessage(kReminders));
    for (auto* button:{fOutboxButton,fMoveButton,fReminderButton}) button->SetFlat(true);
    if (auto* icon=MailIcon("send")) fOutboxButton->SetIcon(icon);
    if (auto* icon=MailIcon("snooze")) fReminderButton->SetIcon(icon);
    if (auto* icon=MailIcon("folder")) fMoveButton->SetIcon(icon);
    auto* toolbar=new BGroupView(B_HORIZONTAL,0);
    BuildToolbar(toolbar);
    BLayoutBuilder::Group<>(this,B_VERTICAL,0)
        .Add(menuBar)
        .Add(toolbar)
        .Add(new BSeparatorView(B_HORIZONTAL))
        .Add(fSplit,1)
        .Add(new BSeparatorView(B_HORIZONTAL))
        .AddGroup(B_HORIZONTAL).SetInsets(10,2,6,2).Add(fStatus,1).Add(fProgress).Add(fMoveButton).Add(fReminderButton).Add(fOutboxButton).End();
    fProgress->Hide(); fAttachmentBox->Hide();
    LoadSettings();
    BMessage tick(kTick);
    fTimer=std::make_unique<BMessageRunner>(BMessenger(this),&tick,30*1000000);
    BMessage draftTick(kDraftTick); fDraftTimer=std::make_unique<BMessageRunner>(BMessenger(this),&draftTick,5*1000000);
    BuildSidebar(); Refresh(); UpdateActions();
    PostMessage(kPollSnoozes);
    PostMessage(kCheckMail);
}
MailWindow::~MailWindow()
{
    fCancel=true;
    fCancelSync=true;
    fCancelSend=true;
    if (fImportWorker.joinable()) fImportWorker.join();
    if (fSyncWorker.joinable()) fSyncWorker.join();
    if (fSendWorker.joinable()) fSendWorker.join();
    std::vector<BListItem*> items; for (int32 i=0;i<fSidebar->FullListCountItems();++i) items.push_back(fSidebar->FullListItemAt(i));
    fSidebar->MakeEmpty(); for (auto* item:items) delete item;
    clear(fConversations); clear(fAttachments);
}
bool MailWindow::QuitRequested() { SaveSettings(); be_app->PostMessage(B_QUIT_REQUESTED); return true; }
void MailWindow::Error(const std::string& message) { (new BAlert("Mail",message.c_str(),"OK",nullptr,nullptr,B_WIDTH_AS_USUAL,B_WARNING_ALERT))->Go(); }

void MailWindow::BuildToolbar(BView* toolbar)
{
    auto button=[&](const char* label,uint32 what,const char* icon,const char* tip) {
        auto* result=new BButton(label,new BMessage(what));
        if (auto* bitmap=MailIcon(icon,24)) result->SetIcon(bitmap);
        result->SetFlat(true); result->SetToolTip(tip); fActions[what]=result; return result;
    };
    fCheckMail=button("Check mail",kCheckMail,"check","Receive new mail from all accounts (Command-M).");
    BLayoutBuilder::Group<>(static_cast<BGroupView*>(toolbar)).SetInsets(6,4,10,4)
        .Add(button("New",kCompose,"new","Write a new message (Command-N)."))
        .Add(fCheckMail)
        .AddStrut(10)
        .Add(button("Reply",kReply,"reply","Reply to the sender (Command-R)."))
        .Add(button("Reply all",kReplyAll,"reply","Reply to everyone (Shift-Command-R)."))
        .Add(button("Forward",kForward,"forward","Forward the message (Shift-Command-F)."))
        .AddStrut(10)
        .Add(button("Archive",kArchive,"archive","Move the selected conversations to the archive (Command-E)."))
        .Add(button("Delete",kTrash,"trash","Move the selected conversations to Trash (Delete)."))
        .Add(button("Unread",kToggleRead,"unread","Mark the selected conversations read or unread."))
        .Add(button("Star",kStar,"star","Star or unstar the selected conversations (Command-L)."))
        .Add(button("Snooze",kSnooze,"snooze","Hide the selected conversations until later (Command-H)."))
        .AddGlue()
        .Add(fSearch);
}
void MailWindow::LoadSettings()
{
    if (fDatabase.empty()) return;
    BFile file((fDatabase+".window").c_str(),B_READ_ONLY); BMessage settings;
    if (file.InitCheck()!=B_OK || settings.Unflatten(&file)!=B_OK) return;
    BRect frame;
    if (settings.FindRect("frame",&frame)==B_OK && BScreen(this).Frame().Intersects(frame) && frame.Width()>400 && frame.Height()>300) {
        MoveTo(frame.LeftTop()); ResizeTo(frame.Width(),frame.Height());
    }
    float weight=0;
    for (int32 i=0;i<3 && settings.FindFloat("split",i,&weight)==B_OK;++i) if (weight>0.05f) fSplit->SetItemWeight(i,weight,i==2);
    const char* key=nullptr;
    for (int32 i=0;settings.FindString("collapsed",i,&key)==B_OK;++i) fCollapsed.insert(key);
    if (settings.FindString("view",&key)==B_OK) fViewName=key;
}
void MailWindow::SaveSettings()
{
    if (fDatabase.empty()) return;
    RememberExpansion();
    BMessage settings; settings.AddRect("frame",Frame());
    for (int32 i=0;i<3;++i) settings.AddFloat("split",fSplit->ItemWeight(i));
    for (const auto& key:fCollapsed) settings.AddString("collapsed",key.c_str());
    auto index=fSidebar->CurrentSelection();
    if (auto* item=dynamic_cast<MailboxItem*>(fSidebar->ItemAt(index)); item && item->View()>=0) settings.AddString("view",fViewKeys[item->View()].c_str());
    BFile file((fDatabase+".window").c_str(),B_WRITE_ONLY|B_CREATE_FILE|B_ERASE_FILE);
    if (file.InitCheck()==B_OK) settings.Flatten(&file);
}

void MailWindow::RememberExpansion()
{
    for (int32 i=0;i<fSidebar->FullListCountItems();++i) {
        auto* item=static_cast<MailboxItem*>(fSidebar->FullListItemAt(i));
        if (!fSidebar->CountItemsUnder(item,true)) continue;
        auto key=item->View()>=0?fViewKeys[item->View()]:std::string("section:")+item->Text();
        if (item->IsExpanded()) fCollapsed.erase(key); else fCollapsed.insert(key);
    }
}
void MailWindow::BuildSidebar()
{
    RememberExpansion();
    std::string selected=fViewName;
    if (auto* item=dynamic_cast<MailboxItem*>(fSidebar->ItemAt(fSidebar->CurrentSelection())); item && item->View()>=0) selected=fViewKeys[item->View()];
    std::vector<BListItem*> old; for (int32 i=0;i<fSidebar->FullListCountItems();++i) old.push_back(fSidebar->FullListItemAt(i));
    fSidebar->MakeEmpty(); for (auto* item:old) delete item;
    fViews.clear(); fViewKeys.clear(); fViewItems.clear();
    MailboxItem* selection=nullptr;
    auto add=[&](MailboxItem* parent,const std::string& label,const char* icon,Query q,std::string key,bool counted=false) {
        auto* item=new MailboxItem(label.c_str(),icon,static_cast<int32>(fViews.size()),parent?parent->OutlineLevel()+1:0);
        if (key==selected) selection=item;
        fSidebar->AddItem(item); // Appended in order; the outline level nests it.
        fViews.push_back(std::move(q)); fViewKeys.push_back(std::move(key)); fViewItems.push_back(counted?item:nullptr);
        return item;
    };
    auto header=[&](const std::string& label) {
        auto* item=new MailboxItem(label.c_str(),nullptr,-1,0,true); fSidebar->AddItem(item);
        if (fCollapsed.count("section:"+label)) item->SetExpanded(false);
        return item;
    };
    Query q; add(nullptr,"All inboxes","mailboxes",q,"builtin:All inboxes",true);
    q.folder=""; q.unread=true; add(nullptr,"Unread","unread",q,"builtin:Unread",true);
    q.unread=false; q.starred=true; add(nullptr,"Starred","star",q,"builtin:Starred");
    q.starred=false; q.snoozedOnly=true; add(nullptr,"Snoozed","snooze",q,"builtin:Snoozed");
    q.snoozedOnly=false; q.attachments=true; add(nullptr,"Attachments","attachment",q,"builtin:Attachments");
    q.attachments=false; q.folder="sent"; add(nullptr,"Sent","send",q,"builtin:Sent");
    q.folder="drafts"; add(nullptr,"Drafts","draft",q,"builtin:Drafts");
    q.folder="archive"; add(nullptr,"Archive","archive",q,"builtin:Archive");
    q.folder="trash"; q.includeSnoozed=true; add(nullptr,"Trash","trash",q,"builtin:Trash");
    auto roleIcon=[](const std::string& role) -> const char* {
        static const std::map<std::string,const char*> icons={{"inbox","inbox"},{"sent","send"},{"drafts","draft"},
            {"trash","trash"},{"junk","junk"},{"archive","archive"}};
        auto found=icons.find(role); return found==icons.end()?"folder":found->second;
    };
    auto accounts=fStore->accounts();
    if (!accounts.empty()) {
        auto* section=header("ACCOUNTS");
        for (const auto& a : accounts) {
            bool local=a.provider==Provider::Local;
            auto remote=fStore->remoteFolders(a.id);
            Query all; all.account=a.id; all.folder="";
            if (local && remote.empty() && fStore->conversations(all,now()).empty()) continue;
            Query inbox; inbox.account=a.id;
            auto key="account:"+std::to_string(a.id);
            auto* account=add(section,a.name,local?"computer":"account",inbox,key,true);
            if (fCollapsed.count(key)) account->SetExpanded(false);
            // Inbox first, then the server's other folders in their own order.
            std::stable_sort(remote.begin(),remote.end(),[](const RemoteFolder& x,const RemoteFolder& y){return (x.role=="inbox")>(y.role=="inbox");});
            for (const auto& folder : remote) {
                if (folder.role=="inbox") continue; // The account row itself shows the inbox.
                Query view; view.account=a.id; view.folder=folder.localName(); view.includeSnoozed=true;
                add(account,folder.name,roleIcon(folder.role),view,"folder:"+std::to_string(a.id)+":"+folder.id,folder.role.empty() || folder.role=="junk");
            }
            add(account,"All mail","folder",all,"all:"+std::to_string(a.id));
            all.snoozedOnly=true; add(account,"Snoozed","snooze",all,"snoozed:"+std::to_string(a.id));
        }
    }
    auto smart=fStore->smartFolders();
    if (!smart.empty()) {
        auto* section=header("SMART FOLDERS");
        for (const auto& folder : smart) add(section,folder.name,"query",folder.query,"smart:"+std::to_string(folder.id));
    }
    auto tags=fStore->tags();
    if (!tags.empty()) {
        auto* section=header("TAGS");
        for (const auto& tag : tags) { Query tagged; tagged.folder=""; tagged.tag=tag; add(section,tag,"tag",tagged,"tag:"+tag); }
    }
    if (!selection) selection=static_cast<MailboxItem*>(fSidebar->FullListItemAt(0));
    // A remembered mailbox inside a collapsed account stays hidden; show it.
    for (auto* parent=fSidebar->Superitem(selection);parent;parent=fSidebar->Superitem(parent)) fSidebar->Expand(parent);
    fQuery=fViews[selection->View()]; fViewName=fViewKeys[selection->View()];
    fSearch->SetText(fQuery.text.c_str());
    fSidebar->Select(fSidebar->IndexOf(selection));
    UpdateCounts();
}
void MailWindow::UpdateCounts()
{
    auto time=now(); std::size_t inbox=0;
    for (std::size_t i=0;i<fViewItems.size();++i) {
        auto* item=fViewItems[i]; if (!item) continue;
        auto count=fStore->unreadConversations(fViews[i],time);
        if (i==0) inbox=count;
        if (count!=item->Count()) { item->SetCount(count); fSidebar->InvalidateItem(fSidebar->IndexOf(item)); }
    }
    std::string title="Mail";
    if (auto* item=dynamic_cast<MailboxItem*>(fSidebar->ItemAt(fSidebar->CurrentSelection()))) title+=std::string(" — ")+item->Text();
    if (inbox) title+=" ("+std::to_string(inbox)+" unread)";
    SetTitle(title.c_str());
}
void MailWindow::SelectView(const std::string& key)
{
    for (int32 i=0;i<fSidebar->FullListCountItems();++i) {
        auto* item=static_cast<MailboxItem*>(fSidebar->FullListItemAt(i));
        if (item->View()<0 || fViewKeys[item->View()]!=key) continue;
        for (auto* parent=fSidebar->Superitem(item);parent;parent=fSidebar->Superitem(parent)) fSidebar->Expand(parent);
        fSidebar->Select(fSidebar->IndexOf(item)); fSidebar->ScrollToSelection();
        fQuery=fViews[item->View()]; fViewName=key; fSearch->SetText(""); Refresh();
        return;
    }
}
void MailWindow::OpenOrganization(int kind,bool create)
{
    auto& messenger=fOrganization[kind];
    if (messenger.IsValid()) {
        BMessage request(create?kOrganizationNew:kOrganizationActivate);
        if (create) {
            request.AddInt64("account",fQuery.account); request.AddString("folder",fQuery.folder.c_str()); request.AddString("search",fQuery.text.c_str()); request.AddString("tag",fQuery.tag.c_str());
            request.AddBool("unread",fQuery.unread); request.AddBool("starred",fQuery.starred); request.AddBool("attachments",fQuery.attachments);
            request.AddBool("snoozed",fQuery.snoozedOnly); request.AddBool("include snoozed",fQuery.includeSnoozed);
        }
        messenger.SendMessage(&request); return;
    }
    auto* window=new OrganizationWindow(this,fStore,static_cast<OrganizationKind>(kind),fQuery,create);
    messenger=BMessenger(window); window->Show();
}
void MailWindow::RefreshOrganization()
{
    BMessage refresh(kOrganizationReload); refresh.AddBool("automatic",true);
    for (auto& item:fOrganization) if(item.second.IsValid()) item.second.SendMessage(&refresh);
}
void MailWindow::Refresh()
{
    RefreshOutbox();
    RefreshReminders();
    std::set<Id> selected;
    for (int32 i=0,index;(index=fConversations->CurrentSelection(i))>=0;++i)
        selected.insert(static_cast<ConversationItem*>(fConversations->ItemAt(index))->conversation.id);
    auto rows=fStore->conversations(fQuery,now());
    if (fQuery.folder=="drafts" && !fQuery.unread && !fQuery.starred && fQuery.tag.empty() && !fQuery.snoozedOnly) for (const auto& d:fStore->drafts(fQuery.account)) {
        if (fQuery.attachments && d.attachments.empty()) continue;
        if (!fQuery.text.empty() && (d.subject+" "+d.to+" "+d.text).find(fQuery.text)==std::string::npos) continue;
        rows.push_back({-d.id,d.account,d.subject.empty()?"(No subject)":d.subject,d.to.empty()?"Draft":d.to,d.text,d.modified,1,0,false,!d.attachments.empty()});
    }
    // Keep the scroll position and selection while the list is rebuilt.
    float scroll=0; if (auto* bar=fConversations->ScrollBar(B_VERTICAL)) scroll=bar->Value();
    fConversations->SetSelectionMessage(nullptr);
    clear(fConversations);
    for (const auto& row : rows) {
        fConversations->AddItem(new ConversationItem(row));
        if (selected.count(row.id)) fConversations->Select(fConversations->CountItems()-1,true);
    }
    fConversations->SetSelectionMessage(new BMessage(kSelect));
    if (auto* bar=fConversations->ScrollBar(B_VERTICAL)) bar->SetValue(scroll);
    std::string empty=rows.empty()?(fQuery.text.empty()?"There are no messages here.":"No messages match your search."):"Select a conversation to read it.";
    if (fConversations->CurrentSelection()<0 && fShownSignature!="empty:"+empty) {
        fShownThread=0; fShownSignature="empty:"+empty;
        fReader->SetEmpty(empty.c_str());
        fTitle->SetText(""); clear(fAttachments); fFiles.clear();
        if (!fAttachmentBox->IsHidden()) fAttachmentBox->Hide();
    }
    std::string title;
    if (auto* item=dynamic_cast<MailboxItem*>(fSidebar->ItemAt(fSidebar->CurrentSelection()))) title=item->Text();
    title+="  ·  "+std::to_string(rows.size())+(rows.size()==1?" conversation":" conversations");
    fListTitle->SetText(title.c_str());
    if (!fSyncing && !fImporting && fSyncError.empty() && !*fStatus->Text() && rows.empty() && fStore->accounts().size()<2)
        fStatus->SetText("Add an account with File → Accounts, or import saved messages with File → Import mail.");
    UpdateCounts(); UpdateActions();
}
void MailWindow::UpdateActions()
{
    int32 count=0; bool received=false;
    for (int32 i=0,index;(index=fConversations->CurrentSelection(i))>=0;++i) {
        ++count; received|=static_cast<ConversationItem*>(fConversations->ItemAt(index))->conversation.id>0;
    }
    auto* item=static_cast<ConversationItem*>(fConversations->ItemAt(fConversations->CurrentSelection()));
    bool single=count==1 && item && item->conversation.id>0;
    for (auto what:{kReply,kReplyAll,kForward}) fActions[what]->SetEnabled(single);
    for (auto what:{kArchive,kTrash,kToggleRead,kStar,kSnooze}) fActions[what]->SetEnabled(received);
    // In Trash, Delete removes messages for good.
    const bool inTrash=fQuery.folder=="trash";
    fTrashItem->SetLabel(inTrash?"Delete permanently…":"Move to Trash");
    fActions[kTrash]->SetToolTip(inTrash?"Delete the selected conversations permanently (Delete)."
        :"Move the selected conversations to Trash (Delete).");
    bool unread=false;
    for (int32 i=0,index;(index=fConversations->CurrentSelection(i))>=0;++i) unread|=static_cast<ConversationItem*>(fConversations->ItemAt(index))->conversation.unread>0;
    auto* toggle=fActions[kToggleRead];
    toggle->SetLabel(unread?"Read":"Unread");
    if (auto* icon=MailIcon(unread?"read":"unread",24)) toggle->SetIcon(icon);
    fFormattedView->SetEnabled(single);
    fPlainButton->SetEnabled(fReader->CanFormat() && count==1);
    fPlainButton->SetLabel(fReader->Plain()?"Formatted":"Plain text");
    if (!fReader->CanFormat() && !fPlainButton->IsHidden()) fPlainButton->Hide();
    if (fReader->CanFormat() && fPlainButton->IsHidden()) fPlainButton->Show();
}
void MailWindow::CheckMail(bool draftsOnly)
{
    if (fAccountsOpen) return;
    SendQueued();
    if (fSyncing) { if (!draftsOnly) { fCancelSync=true; fStatus->SetText("Stopping mail check…"); } return; }
    if (fSyncWorker.joinable()) fSyncWorker.join();
    auto accounts=fStore->accounts();
    accounts.erase(std::remove_if(accounts.begin(),accounts.end(),[&](const Account& a){return a.provider==Provider::Local || (draftsOnly && a.provider!=Provider::Gmail);}),accounts.end());
    if (!draftsOnly) fLastSync=now();
    fDraftDue=0;
    if (draftsOnly && accounts.empty()) return;
    if (accounts.empty()) { fStatus->SetText("Add an account with File → Accounts to receive mail."); return; }
    fSyncError.clear(); fStatus->SetToolTip("");
    fCancelSync=false; fSyncing=true;
    if (!draftsOnly) { SetBusy(true); fStatus->SetText(""); }
    auto store=fStore; BMessenger target(this);
    fSyncWorker=std::thread([this,store,target,accounts,draftsOnly] {
        int32 downloaded=0; std::string errors;
        for (std::size_t index=0;index<accounts.size();++index) {
            const auto& account=accounts[index];
            if (fCancelSync) break;
            try {
                auto progress=[&](const SyncProgress& state) {
                    if (draftsOnly) return;
                    BMessage progress(kSyncProgress); progress.AddString("text",state.text.c_str());
                    progress.AddInt32("account",index); progress.AddInt32("accounts",accounts.size());
                    progress.AddInt32("step",state.step); progress.AddInt32("steps",state.steps);
                    progress.AddInt32("done",state.done); progress.AddInt32("total",state.total);
                    target.SendMessage(&progress);
                };
                SyncReport report;
                if (account.provider==Provider::Gmail) {
                    auto api=ConnectGmail(account,fCancelSync,fGoogle.endpoints);
                    report=draftsOnly?syncGmailDrafts(*store,account,*api,fCancelSync):syncGmailAccount(*store,account,*api,fCancelSync,progress);
                } else report=syncAccount(*store,account,LoadCredentials(account).incoming,fCancelSync,progress);
                downloaded+=static_cast<int32>(report.downloaded);
                for (const auto& error : report.errors) errors+=account.name+": "+error+'\n';
            } catch (const std::exception& error) { if (!fCancelSync) errors+=account.name+": "+error.what()+'\n'; }
        }
        BMessage done(kSyncDone); done.AddInt32("downloaded",downloaded); done.AddString("errors",errors.c_str());
        done.AddBool("cancelled",fCancelSync); done.AddBool("drafts",draftsOnly); target.SendMessage(&done);
    });
}
void MailWindow::SetBusy(bool busy)
{
    fCheckMail->SetLabel(busy?"Stop":"Check mail");
    fCheckMail->SetToolTip(busy?"Stop checking mail.":"Receive new mail from all accounts (Command-M).");
    if (busy) { fProgress->Reset(); fProgress->SetMaxValue(1000); if (fProgress->IsHidden()) fProgress->Show(); }
    else if (!fProgress->IsHidden()) fProgress->Hide();
}
void MailWindow::ShowSyncProgress(BMessage* message)
{
    if (!fSyncing) return;
    auto account=message->GetInt32("account",0),accounts=std::max<int32>(1,message->GetInt32("accounts",1));
    auto step=message->GetInt32("step",0),steps=message->GetInt32("steps",0);
    auto done=message->GetInt32("done",0),total=message->GetInt32("total",0);
    // Accounts share the bar equally; within one, folders do; within a folder, messages.
    float part=0;
    if (steps>0) part=(std::max<int32>(step-1,0)+(total>0?float(done)/total:0.f))/steps;
    else if (total>0) part=float(done)/total;
    float value=1000.f*(account+std::min(part,1.f))/accounts;
    std::string trailing;
    if (total>0) trailing=std::to_string(done+1)+" of "+std::to_string(total);
    else if (done>0) trailing=std::to_string(done)+" messages";
    fProgress->SetTo(value,message->GetString("text",""),trailing.c_str());
    fProgress->SetToolTip(message->GetString("text",""));
}
void MailWindow::ShowConversation()
{
    UpdateActions();
    int32 count=0; for (int32 i=0;fConversations->CurrentSelection(i)>=0;++i) ++count;
    auto* item=static_cast<ConversationItem*>(fConversations->ItemAt(fConversations->CurrentSelection()));
    if (!item) return;
    if (count>1) {
        fShownThread=0; fShownSignature="multiple";
        fTitle->SetText((std::to_string(count)+" conversations selected").c_str());
        fReader->SetEmpty("Use the toolbar or the Message menu to act on all selected conversations.");
        clear(fAttachments); fFiles.clear(); if (!fAttachmentBox->IsHidden()) fAttachmentBox->Hide();
        return;
    }
    auto showFiles=[&] {
        clear(fAttachments);
        for (const auto& a:fFiles) fAttachments->AddItem(new AttachmentItem(a.name+"  ("+std::to_string((a.data.size()+1023)/1024)+" KB)"));
        bool any=!fFiles.empty();
        if (any && fAttachmentBox->IsHidden()) fAttachmentBox->Show();
        if (!any && !fAttachmentBox->IsHidden()) fAttachmentBox->Hide();
        fAttachmentSave->SetEnabled(false); fAttachmentPreview->SetEnabled(false);
    };
    if (item->conversation.id<0) {
        auto d=fStore->draft(-item->conversation.id); fTitle->SetText(d.subject.empty()?"Draft":d.subject.c_str());
        fShownThread=item->conversation.id; fShownSignature.clear();
        fReader->SetDraft(d,"Draft — double-click to continue editing\nTo: "+d.to+"\nCc: "+d.cc+"\nBcc: "+d.bcc+"\n\n"+d.text);
        fFiles=d.attachments; showFiles();
        return;
    }
    auto messages=fStore->thread(item->conversation.id);
    // Rebuilding the list (for example on the periodic refresh) re-selects the
    // same conversation; keep the reader and its scroll position then.
    std::string signature;
    for (const auto& m : messages) { signature+=std::to_string(m.id)+":"+std::to_string(m.snoozedUntil)+":"+m.folder+":"; for (const auto& t:m.tags) signature+=t+","; signature+=";"; }
    if (item->conversation.id==fShownThread && signature==fShownSignature) return;
    fShownThread=item->conversation.id; fShownSignature=signature;
    std::string text; fFiles.clear();
    std::vector<ReceivedPreview> previews; std::set<Id> expanded;
    for (const auto& m : messages) {
        text+=m.sender+"\nTo: "+m.to;
        if (!m.cc.empty()) text+="\nCc: "+m.cc;
        text+='\n'+date(m.received);
        if(m.snoozedUntil>now()) text+="\nSnoozed until: "+describeSnoozeTime(m.snoozedUntil);
        if (!m.tags.empty()) { text+="\nTags: "; for (const auto& t : m.tags) text+=t+"  "; }
        text+="\n\n"+m.text+"\n\n\n";
        ReceivedPreview preview; preview.id=m.id; preview.from=m.sender; preview.to=m.to; preview.cc=m.cc; preview.subject=m.subject;
        preview.date=date(m.received); preview.text=m.text; preview.html=m.html; preview.files=m.attachments;
        previews.push_back(std::move(preview));
        if (m.unread) expanded.insert(m.id);
        for (const auto& a : m.attachments) if (!a.inlineImage || !previewImageType(a.mediaType)) fFiles.push_back(a);
    }
    std::string title=item->conversation.subject.empty()?"(No subject)":item->conversation.subject;
    fTitle->SetText(title.c_str()); fTitle->SetToolTip(title.c_str());
    fReader->SetConversation(std::move(previews),text,expanded);
    showFiles();
    // Mark the conversation read once it has been shown for a moment, so
    // moving through the list with the arrow keys does not mark everything.
    fReadTimer.reset();
    if (item->conversation.unread>0) {
        BMessage read(kMarkShownRead); read.AddInt64("thread",item->conversation.id);
        fReadTimer=std::make_unique<BMessageRunner>(BMessenger(this),&read,1500000,1);
    }
}
void MailWindow::OpenFormatted(Id thread,Id message)
{
    if(!thread) {
        auto* item=static_cast<ConversationItem*>(fConversations->ItemAt(fConversations->CurrentSelection()));
        if(!item || item->conversation.id<=0) return;
        thread=item->conversation.id;
    }
    auto messages=fStore->thread(thread); if(messages.empty()) throw std::runtime_error("This conversation is no longer available.");
    auto found=message?std::find_if(messages.begin(),messages.end(),[&](const auto& m){return m.id==message;}):std::prev(messages.end());
    if(found==messages.end()) throw std::runtime_error("This message is no longer in the conversation.");
    const auto& m=*found; PreviewSnapshot snapshot; snapshot.received.emplace(); auto& received=*snapshot.received;
    received.id=m.id; received.from=m.sender; received.to=m.to; received.cc=m.cc; received.subject=m.subject;
    received.date=date(m.received); received.text=m.text; received.html=m.html; received.files=m.attachments;
    for(const auto& entry:messages) received.thread.push_back({entry.id,entry.sender,date(entry.received)});
    for(const auto& [key,id]:fFormattedThreads) if(id==thread) {snapshot.key=key;break;}
    if(snapshot.key.empty()) snapshot.key=newMessageId();
    LaunchPreview(snapshot,BMessenger(this)); fFormattedThreads[snapshot.key]=thread;
}
void MailWindow::OpenLink(const std::string& url)
{
    if (!openableLink(url)) return;
    if (url.size()>7 && (url.compare(0,7,"mailto:")==0 || url.compare(0,7,"MAILTO:")==0)) {
        auto decode=[](const std::string& value) {
            std::string decoded;
            for (std::size_t i=0;i<value.size();++i) {
                if (value[i]=='%' && i+2<value.size() && std::isxdigit(static_cast<unsigned char>(value[i+1])) && std::isxdigit(static_cast<unsigned char>(value[i+2])))
                    { decoded+=static_cast<char>(std::stoi(value.substr(i+1,2),nullptr,16)); i+=2; }
                else decoded+=value[i]=='+'?' ':value[i];
            }
            return decoded;
        };
        auto query=url.find('?');
        std::string subject;
        if (query!=std::string::npos) for (std::size_t at=query+1;at<url.size();) {
            auto end=url.find('&',at); if (end==std::string::npos) end=url.size();
            auto field=url.substr(at,end-at);
            if (field.size()>8 && strncasecmp(field.c_str(),"subject=",8)==0) subject=decode(field.substr(8));
            at=end+1;
        }
        Compose(kCompose,0,decode(url.substr(7,query==std::string::npos?std::string::npos:query-7)),subject); return;
    }
    if (BUrl(url.c_str(),false).OpenWithPreferredApplication(false)!=B_OK)
        Error("Could not open the link. Choose a preferred web browser in FileTypes.\n\n"+url);
}
std::vector<Id> MailWindow::SelectedMessages()
{
    std::vector<Id> ids;
    for (int32 i=0,index;(index=fConversations->CurrentSelection(i))>=0;++i) {
        auto* item=static_cast<ConversationItem*>(fConversations->ItemAt(index));
        if (item->conversation.id<=0) continue;
        for (const auto& m : fStore->thread(item->conversation.id)) ids.push_back(m.id);
    }
    return ids;
}
void MailWindow::Apply(uint32 action)
{
    auto ids=SelectedMessages();
    if (ids.empty()) return;
    if(action==kQuickSnooze || action==kWake) {
        fStore->snooze(ids,action==kWake?0:snoozePreset(now(),SnoozePreset::Hour)); Refresh(); ShowConversation(); return;
    }
    if (action==kToggleRead) {
        bool unread=false; for (Id id:ids) unread|=fStore->message(id).unread;
        action=unread?kRead:kUnread;
    }
    if (action==kTrash && fQuery.folder=="trash") {
        // Already in Trash: delete for good. Only the trashed messages of a
        // conversation go; its other messages are not shown in this view.
        std::vector<Id> trashed;
        for (Id id:ids) if (fStore->message(id).folder=="trash") trashed.push_back(id);
        if (trashed.empty() || !ConfirmDelete(trashed,false)) return;
        ids=trashed; action=kDeleteForever;
    }
    // Moving conversations out of the view selects the next one, as Tracker does.
    int32 next=-1;
    if (action==kArchive || action==kTrash || action==kInbox || action==kDeleteForever) {
        int32 last=-1; for (int32 i=0,index;(index=fConversations->CurrentSelection(i))>=0;++i) last=index;
        next=last+1;
    }
    bool starred=false; if (action==kStar) for (Id id:ids) starred|=fStore->message(id).starred;
    if (action==kDeleteForever) {
        try { fStore->deletePermanently(ids); }
        catch (const std::exception& error) { Error(error.what()); return; }
        fStatus->SetText(ids.size()==1?"Message deleted.":"Messages deleted.");
    } else for (Id id : ids) {
        switch (action) {
            case kArchive: fStore->move(id,"archive"); break;
            case kTrash: fStore->move(id,"trash"); break;
            case kInbox: fStore->move(id,"inbox"); break;
            case kRead: fStore->setRead(id,true); break;
            case kUnread: fStore->setRead(id,false); break;
            case kStar: fStore->setStarred(id,!starred); break;
        }
    }
    if (action==kUnread) fReadTimer.reset();
    Id nextId=0;
    if (next>=0) if (auto* item=static_cast<ConversationItem*>(fConversations->ItemAt(next))) nextId=item->conversation.id;
    if (next>=0) fConversations->DeselectAll();
    Refresh();
    if (nextId) for (int32 i=0;i<fConversations->CountItems();++i) if (static_cast<ConversationItem*>(fConversations->ItemAt(i))->conversation.id==nextId) {
        fConversations->Select(i); fConversations->ScrollToSelection(); break;
    }
    ShowConversation();
}
/*! Asks before messages are deleted for good. \a whole: all of Trash. */
bool MailWindow::ConfirmDelete(const std::vector<Id>& messages,bool whole)
{
    bool gmail=false,pop=false; std::map<Id,Provider> providers;
    for (const auto& account:fStore->accounts()) providers[account.id]=account.provider;
    for (Id id:messages) {
        auto provider=providers[fStore->message(id).account];
        gmail|=provider==Provider::Gmail; pop|=provider==Provider::Pop3;
        if (gmail && pop) break;
    }
    std::string text=whole?"Delete all "+std::to_string(messages.size())+" messages in Trash permanently?"
        :messages.size()==1?std::string("Delete this message permanently?")
        :"Delete these "+std::to_string(messages.size())+" messages permanently?";
    if (whole && messages.size()==1) text="Delete the message in Trash permanently?";
    text+="\n\nThis cannot be undone.";
    if (gmail) text+="\n\nGmail keeps deleted messages in its own Trash for up to 30 days before it removes them.";
    if (pop) text+="\n\nPOP3 mail is deleted on this computer only; the server keeps its copy.";
    auto* alert=new BAlert("Delete permanently",text.c_str(),"Cancel","Delete permanently",nullptr,
        B_WIDTH_AS_USUAL,B_WARNING_ALERT);
    alert->SetShortcut(0,B_ESCAPE); alert->SetDefaultButton(alert->ButtonAt(0));
    return alert->Go()==1;
}
void MailWindow::EmptyTrash()
{
    // The Trash being shown, else the Trash of all accounts.
    Id account=fQuery.folder=="trash"?fQuery.account:0;
    auto messages=fStore->trashed(account);
    if (messages.empty()) { fStatus->SetText("Trash is empty."); return; }
    if (!ConfirmDelete(messages,true)) return;
    try { fStore->deletePermanently(messages); }
    catch (const std::exception& error) { Error(error.what()); return; }
    fConversations->DeselectAll(); Refresh(); ShowConversation();
    fStatus->SetText("Trash emptied.");
}
void MailWindow::RefreshReminders(bool notify)
{
    auto reminders=fStore->reminders(now()); std::set<Id> conversations,pending; std::vector<Id> ids;
    for(const auto& reminder:reminders) {
        conversations.insert(reminder.thread);
        if(!reminder.notified) { pending.insert(reminder.thread); ids.push_back(reminder.id); }
    }
    fReminderButton->SetLabel(("Reminders ("+std::to_string(conversations.size())+")").c_str());
    fReminderButton->SetEnabled(!conversations.empty());
    if (conversations.empty() && !fReminderButton->IsHidden()) fReminderButton->Hide();
    if (!conversations.empty() && fReminderButton->IsHidden()) fReminderButton->Show();
    if(notify && !ids.empty() && NotifySnoozes(pending.size(),fDatabase)) fStore->markRemindersNotified(ids);
    if(fReminders.IsValid()) fReminders.SendMessage(kReminderReload);
}
void MailWindow::OpenSnooze(const std::vector<Id>& ids)
{
    if(ids.empty()) { fStatus->SetText("Select a conversation to snooze."); return; }
    if(fAccountsOpen) { fStatus->SetText("Close Accounts before choosing a snooze time."); return; }
    if(fSnooze.IsValid()) { fSnooze.SendMessage(kSnoozeActivate); return; }
    auto* window=new SnoozeWindow(this,fStore,ids,fStore->message(ids[0]).snoozedUntil); fSnooze=BMessenger(window); window->Show();
}
void MailWindow::OpenReminder(Id id,bool again)
{
    auto reminders=fStore->reminders(now());
    auto found=std::find_if(reminders.begin(),reminders.end(),[&](const SnoozeReminder& reminder){return reminder.id==id;});
    if(found==reminders.end()) { RefreshReminders(); return; }
    auto message=fStore->message(found->message); // Resolve current thread after any merge.
    if(again) {
        std::vector<Id> ids; for(const auto& member:fStore->thread(message.thread)) ids.push_back(member.id);
        OpenSnooze(ids); return;
    }
    SelectView("all:"+std::to_string(message.account));
    for(int32 i=0;i<fConversations->CountItems();++i) if(static_cast<ConversationItem*>(fConversations->ItemAt(i))->conversation.id==message.thread) { fConversations->Select(i); ShowConversation(); break; }
    Activate();
}
void MailWindow::Import(BMessage* message)
{
    if (fImporting) { Error("Mail is still importing the previous files."); return; }
    std::vector<std::string> paths; entry_ref ref;
    for (int32 i=0;message->FindRef("refs",i,&ref)==B_OK;++i) {
        BEntry entry(&ref,true); BPath path;
        if (entry.GetPath(&path)==B_OK && entry.IsFile()) paths.emplace_back(path.Path());
    }
    if (paths.empty()) return;
    if (fImportWorker.joinable()) fImportWorker.join();
    fImporting=true; fStatus->SetText("");
    fProgress->Reset(); fProgress->SetMaxValue(paths.size()); if (fProgress->IsHidden()) fProgress->Show();
    auto store=fStore; auto account=fLocalAccount; BMessenger target(this);
    fImportWorker=std::thread([this,store,account,target,paths] {
        int32 imported=0,failed=0; std::string firstError;
        for (const auto& path : paths) {
            if (fCancel) break;
            BMessage progress(kSyncProgress); progress.AddString("text","Importing mail"); progress.AddInt32("done",imported+failed);
            progress.AddInt32("total",paths.size()); progress.AddBool("import",true); target.SendMessage(&progress);
            try {
                std::ifstream file(path,std::ios::binary|std::ios::ate);
                auto size=file.tellg();
                if (!file || size<0 || size>32*1024*1024) throw std::runtime_error("Could not read the file, or it exceeds 32 MiB.");
                std::string raw(static_cast<std::size_t>(size),'\0'); file.seekg(0);
                if (!file.read(raw.data(),size)) throw std::runtime_error("Could not read the complete message.");
                auto m=parseMessage(raw); m.account=account; m.remoteKey="file:"+path;
                if (!m.received) m.received=now();
                store->ingest(m); ++imported;
            } catch (const std::exception& error) { ++failed; if (firstError.empty()) firstError=path+"\n"+error.what(); }
        }
        BMessage done(kImported); done.AddInt32("count",imported); done.AddInt32("failed",failed);
        done.AddString("error",firstError.c_str()); target.SendMessage(&done);
    });
}
void MailWindow::Compose(uint32 action,Id id,const std::string& to,const std::string& subject)
{
    auto* selected=static_cast<ConversationItem*>(fConversations->ItemAt(fConversations->CurrentSelection()));
    if (action==kOpenDraft) {
        if (!id && selected && selected->conversation.id<0) id=-selected->conversation.id;
        if (!id) return;
        auto found=fComposers.find(id); if (found!=fComposers.end() && found->second.IsValid()) { found->second.SendMessage(kComposeActivate); return; }
    }
    Draft d;
    if (id) d=fStore->draft(id);
    else {
        auto accounts=fStore->accounts(); Account account=accounts.front();
        // Write from the account being viewed, otherwise from the first one that can send.
        for (const auto& a:accounts) if (a.provider!=Provider::Local) { account=a; break; }
        for (const auto& a:accounts) if (a.id==fQuery.account && a.provider!=Provider::Local) account=a;
        if (action==kReply || action==kReplyAll || action==kForward) {
            if (!selected || selected->conversation.id<=0) { Error("Select a received conversation first."); return; }
            auto thread=fStore->thread(selected->conversation.id); if (thread.empty()) return;
            auto m=thread.back();
            for (const auto& a:accounts) if (a.id==m.account && a.provider!=Provider::Local) account=a;
            d=action==kForward?forwardDraft(m,account):replyDraft(m,account,action==kReplyAll);
        } else { d.account=account.id; d.messageId=newMessageId(); d.to=to; d.subject=subject; }
        d.modified=now(); d.id=fStore->saveDraft(d);
    }
    auto* window=new ComposerWindow(this,fStore,d); fComposers[d.id]=BMessenger(window); window->Show(); Refresh();
    fDraftDue=now()+10;
}
void MailWindow::SendQueued()
{
    if (fSending || fAccountsOpen) return;
    if (!fStore->readyOutgoing()) return;
    if (fSendWorker.joinable()) fSendWorker.join();
    fSending=true; fCancelSend=false; fStatus->SetText("Sending mail…"); auto store=fStore; BMessenger target(this);
    fSendWorker=std::thread([this,store,target] {
        std::string status; bool sent=false;
        try {
            auto report=deliverQueued(*store,[](const Account& a){return LoadCredentials(a).outgoing;},fCancelSend,
                [&](const Account& a){return ConnectGmail(a,fCancelSend,fGoogle.endpoints);});
            sent=report.sent>0; status=report.sent==1?"1 message sent.":std::to_string(report.sent)+" messages sent.";
            if (report.failed || report.uncertain) status+=" Open the Outbox to review delivery problems.";
        } catch (const std::exception& error) { status=error.what(); }
        BMessage done(kSendDone); done.AddBool("sent",sent); done.AddString("status",status.c_str()); target.SendMessage(&done);
    });
}
void MailWindow::RefreshOutbox()
{
    auto moves=fStore->moveReviews();
    fMoveButton->SetLabel(("Folder moves ("+std::to_string(moves.size())+")").c_str());
    fMoveButton->SetToolTip("Review queued or interrupted IMAP folder moves.");
    if (moves.empty() && !fMoveButton->IsHidden()) fMoveButton->Hide();
    if (!moves.empty() && fMoveButton->IsHidden()) fMoveButton->Show();
    auto summary=fStore->outboxSummary();
    std::string label="Outbox ("+std::to_string(summary.total());
    if (summary.attention()) label+=" · "+std::to_string(summary.attention())+(summary.attention()==1?" needs attention":" need attention");
    label+=")";
    auto copies=fStore->sentCopies();
    if (!copies.empty()) label+=" · "+std::to_string(copies.size())+(copies.size()==1?" Sent copy":" Sent copies");
    fOutboxButton->SetLabel(label.c_str());
    bool visible=summary.total() || !copies.empty();
    if (!visible && !fOutboxButton->IsHidden()) fOutboxButton->Hide();
    if (visible && fOutboxButton->IsHidden()) fOutboxButton->Show();
    std::string details=std::to_string(summary.queued)+" waiting to send\n"+std::to_string(summary.sending)+" sending\n"
        +std::to_string(summary.failed)+" failed\n"+std::to_string(summary.uncertain)+" with uncertain delivery";
    if (summary.attention()) details+="\nOpen Outbox to review. These messages will not be retried automatically.";
    if (!copies.empty()) details+="\n"+std::to_string(copies.size())+" delivered messages await server Sent copies. Open Outbox to review.";
    fOutboxButton->SetToolTip(details.c_str());
}
void MailWindow::MessageReceived(BMessage* message)
{
    try {
        switch (message->what) {
            case kCompose: case kReply: case kReplyAll: case kForward: Compose(message->what); break;
            case kOpenDraft: {
                int64 id=0; message->FindInt64("draft",&id);
                auto* item=static_cast<ConversationItem*>(fConversations->ItemAt(fConversations->CurrentSelection()));
                if(!id && item && item->conversation.id>0) OpenFormatted(item->conversation.id);
                else Compose(kOpenDraft,id);
                break;
            }
            case kFormatted: OpenFormatted(); break;
            case kPlainText: fReader->SetPlain(!fReader->Plain()); UpdateActions(); break;
            case kReaderLink: OpenLink(message->GetString("url","")); break;
            case kReaderChanged: UpdateActions(); break;
            case kMakeDefault: {
                if ((new BAlert("Default email application","Open email messages and mailto links with this Mail? Haiku's previous Mail stays installed and can be chosen again in FileTypes.",
                    "Cancel","Make default",nullptr,B_WIDTH_AS_USUAL,B_INFO_ALERT))->Go()!=1) break;
                bool ok=true;
                for (const char* type:{"text/x-email","message/rfc822","application/x-vnd.Be.URL.mailto"}) {
                    BMimeType mime(type); if (!mime.IsInstalled()) mime.Install();
                    ok&=mime.SetPreferredApp("application/x-vnd.air-Mail")==B_OK;
                }
                fStatus->SetText(ok?"Mail is now the default email application.":"Some email types could not be changed. Use FileTypes to choose Mail.");
                break;
            }
            case kMarkShownRead: {
                Id thread=message->GetInt64("thread",0); fReadTimer.reset();
                if (!thread || thread!=fShownThread) break;
                for (const auto& m:fStore->thread(thread)) if (m.unread) fStore->setRead(m.id,true);
                Refresh();
                break;
            }
            case kPreviewRefresh: case kPreviewSelect: {
                auto found=fFormattedThreads.find(message->GetString("preview-key",""));
                if(found!=fFormattedThreads.end()) OpenFormatted(found->second,message->GetInt64("message",0));
                break;
            }
            case kDraftChanged: Refresh(); fDraftDue=now()+10; break;
            case kDraftReview:
                if (fDraftReview.IsValid()) fDraftReview.SendMessage(kDraftReview);
                else { auto* window=new DraftWindow(this,fStore); fDraftReview=BMessenger(window); window->Show(); }
                break;
            case kDraftReviewChanged: Refresh(); fDraftDue=now(); break;
            case kCheckDrafts:
                fDraftDue=now(); if (!fSyncing && !fAccountsOpen) CheckMail(true);
                break;
            case kDraftTick:
                if (fDraftDue && now()>=fDraftDue && !fSyncing && !fAccountsOpen) CheckMail(true);
                break;
            case kOutbox:
                if (fOutbox.IsValid()) fOutbox.SendMessage(kOutboxActivate);
                else { auto* window=new OutboxWindow(this,fStore); fOutbox=BMessenger(window); window->Show(); }
                break;
            case kFolderMoves:
                if (fMoves.IsValid()) fMoves.SendMessage(kFolderMoves);
                else { auto* window=new MoveWindow(this,fStore); fMoves=BMessenger(window); window->Show(); }
                break;
            case kMovesChanged: BuildSidebar(); Refresh(); ShowConversation(); break;
            case kOutboxChanged: Refresh(); break;
            case kSendQueued: Refresh(); SendQueued(); break;
            case kSendDone: {
                fSending=false; Refresh(); const char* status=""; message->FindString("status",&status); fStatus->SetText(status);
                // A composer may have queued another message while the previous worker was finishing.
                bool sent=false; message->FindBool("sent",&sent);
                if (sent && !fStore->sentCopies().empty()) fNeedsSentCheck=true;
                SendQueued();
                if (fNeedsSentCheck && !fSyncing && !fSending) { fNeedsSentCheck=false; CheckMail(); }
                break;
            }
            case kCheckSentCopies: case kCheckMoves:
                if (fSyncing) fNeedsSentCheck=true;
                else CheckMail();
                break;
            case kOrganizationChanged: BuildSidebar(); Refresh(); ShowConversation(); RefreshOrganization(); break;
            case kSnooze: OpenSnooze(SelectedMessages()); break;
            case kSnoozeChanged: Refresh(); ShowConversation(); break;
            case kPollSnoozes: RefreshReminders(true); break;
            case kReminders:
                if(fReminders.IsValid()) fReminders.SendMessage(kReminders);
                else { auto* window=new ReminderWindow(this,fStore); fReminders=BMessenger(window); window->Show(); }
                break;
            case kReminderOpen: case kReminderAgain: {
                int64 id=0; message->FindInt64("reminder",&id); OpenReminder(id,message->what==kReminderAgain); break;
            }
            case kManageRules: OpenOrganization(static_cast<int>(OrganizationKind::Rules)); break;
            case kManageSmart: OpenOrganization(static_cast<int>(OrganizationKind::SmartFolders)); break;
            case kManageTags: OpenOrganization(static_cast<int>(OrganizationKind::Tags)); break;
            case kAccounts:
                if(fSnooze.IsValid()) fStatus->SetText("Finish choosing a snooze time before changing accounts.");
                else if (std::any_of(fOrganization.begin(),fOrganization.end(),[](const auto& item){return item.second.IsValid();}))
                    fStatus->SetText("Close the organization editors before changing accounts.");
                else if (fSyncing || fSending) {
                    // Opening Accounts stops the check; the window opens once it has finished.
                    fCancelSync=true; fStatus->SetText("Stopping the mail check to edit accounts…");
                    BMessage retry(kAccounts); BMessageRunner::StartSending(BMessenger(this),&retry,300000,1);
                }
                else if (std::any_of(fComposers.begin(),fComposers.end(),[](const auto& item){return item.second.IsValid();}))
                    fStatus->SetText("Close your message drafts before editing accounts. They will be saved in Drafts.");
                else if (!fAccountsOpen) { (new AccountWindow(this,fStore,fGoogle))->Show(); fAccountsOpen=true; }
                break;
            case kAccountsChanged: BuildSidebar(); Refresh(); break;
            case kAccountsClosed: fAccountsOpen=false; CheckMail(); break;
            case kCheckMail: CheckMail(); break;
            case kSyncProgress:
                if (message->GetBool("import",false)) {
                    auto done=message->GetInt32("done",0),total=message->GetInt32("total",0);
                    fProgress->SetTo(done,"Importing mail",(std::to_string(done+1)+" of "+std::to_string(total)).c_str());
                } else ShowSyncProgress(message);
                break;
            case kSyncDone: {
                bool drafts=message->GetBool("drafts",false);
                fSyncing=false; if (!drafts) SetBusy(false);
                BuildSidebar(); Refresh(); RefreshOrganization();
                int32 downloaded=0; bool cancelled=false; const char* errors="";
                message->FindInt32("downloaded",&downloaded); message->FindBool("cancelled",&cancelled); message->FindString("errors",&errors);
                if (!drafts || (errors && *errors)) {
                    char time[16]; std::time_t value=std::time(nullptr); std::tm tm{}; localtime_r(&value,&tm); std::strftime(time,sizeof(time),"%H:%M",&tm);
                    std::string status=cancelled?"Mail check stopped.":std::string("Checked at ")+time+" · "
                        +(downloaded==0?"no new messages":downloaded==1?"1 new message":std::to_string(downloaded)+" new messages");
                    if (errors && *errors) {
                        status=errors; status=status.substr(0,status.find('\n')); fSyncError=status;
                        if (std::count(errors,errors+strlen(errors),'\n')>1) status+="  (more in tooltip)";
                    }
                    fStatus->SetText(status.c_str()); fStatus->SetToolTip(errors?errors:"");
                }
                SendQueued();
                if (fNeedsSentCheck && !fSending) {
                    fNeedsSentCheck=false; if (!cancelled) CheckMail();
                }
                break;
            }
            case kImport:
                if (!fImportPanel) { BMessenger target(this); fImportPanel=std::make_unique<BFilePanel>(B_OPEN_PANEL,&target,nullptr,B_FILE_NODE,true); }
                fImportPanel->Show(); break;
            case B_REFS_RECEIVED: case B_SIMPLE_DATA: Import(message); break;
            case kImported: {
                fImporting=false; if (!fSyncing && !fProgress->IsHidden()) fProgress->Hide();
                BuildSidebar(); Refresh();
                int32 count=0,failed=0; message->FindInt32("count",&count); message->FindInt32("failed",&failed);
                auto status=count==1?std::string("Imported 1 message."):"Imported "+std::to_string(count)+" messages.";
                fStatus->SetText(status.c_str());
                if (failed) Error(std::to_string(failed)+(failed==1?" file":" files")+" could not be imported.\n\n"+message->FindString("error"));
                break;
            }
            case kView: {
                auto* item=dynamic_cast<MailboxItem*>(fSidebar->ItemAt(fSidebar->CurrentSelection()));
                if (item && item->View()>=0) {
                    fQuery=fViews[item->View()]; fViewName=fViewKeys[item->View()]; fSearch->SetText(fQuery.text.c_str());
                    fConversations->DeselectAll(); fConversations->ScrollTo(0,0); Refresh();
                } else if (item) {
                    // Section headings are not mailboxes. Keep the current view selected.
                    SelectView(fViewName);
                }
                break;
            }
            case kSelect: ShowConversation(); break;
            case kSearch: fQuery.text=fSearch->Text(); Refresh(); break;
            case kTick:
                RefreshReminders(true);
                if (!fImporting) Refresh();
                if (!fSyncing && now()-fLastSync>=300) CheckMail();
                break;
            case kArchive: case kTrash: case kInbox: case kRead: case kUnread: case kToggleRead:
            case kStar: case kQuickSnooze: case kWake: Apply(message->what); break;
            case kEmptyTrash: EmptyTrash(); break;
            case kTag: case kRemoveTag: {
                const char* value=nullptr;
                if (message->FindString("value",&value)==B_OK) {
                    for (Id id : SelectedMessages()) fStore->tag(id,value,message->what==kTag);
                    BuildSidebar(); Refresh(); ShowConversation(); RefreshOrganization();
                } else if (!SelectedMessages().empty()) (new EditWindow(this,message->what,message->what==kTag?"Add tag":"Remove tag","Tag:"))->Show();
                break;
            }
            case kSaveSmart: {
                const char* value=nullptr;
                if (message->FindString("value",&value)==B_OK) {
                    fStore->saveSmartFolder({0,value,fQuery}); BuildSidebar();
                } else OpenOrganization(static_cast<int>(OrganizationKind::SmartFolders),true);
                break;
            }
            case kSaveFilter: {
                const char* value=nullptr;
                if (message->FindString("value",&value)==B_OK) {
                    Filter f; f.name=value; f.account=fQuery.account; f.senderContains=message->FindString("sender");
                    f.subjectContains=message->FindString("subject"); f.addTag=message->FindString("tag");
                    bool archive=false; message->FindBool("archive",&archive); if (archive) f.moveTo="archive";
                    message->FindBool("read",&f.markRead); fStore->saveFilter(f);
                    fStatus->SetText("Filter saved. It will apply to new messages.");
                } else OpenOrganization(static_cast<int>(OrganizationKind::Rules),true);
                break;
            }
            case kAttachmentSelected: {
                auto index=fAttachments->CurrentSelection(); bool selected=index>=0 && static_cast<std::size_t>(index)<fFiles.size();
                fAttachmentSave->SetEnabled(selected); fAttachmentPreview->SetEnabled(selected && fFiles[index].mediaType.rfind("image/",0)==0); break;
            }
            case kPreviewAttachment: {
                auto index=fAttachments->CurrentSelection();
                if (index>=0 && static_cast<std::size_t>(index)<fFiles.size() && fFiles[index].mediaType.rfind("image/",0)==0)
                    (new AttachmentPreviewWindow(fFiles[index]))->Show();
                break;
            }
            case kAttachmentExported: {
                const char* text=nullptr; if (message->FindString("text",&text)==B_OK) {
                    fStatus->SetText(text); if (message->GetBool("failed",false)) Error(text);
                } break;
            }
            case kSaveAttachment: {
                auto index=fAttachments->CurrentSelection();
                if (index<0 || static_cast<std::size_t>(index)>=fFiles.size()) break;
                fSaving=fFiles[index]; BMessenger target(this);
                BMessage save(kAttachmentSaved);
                fSavePanel=std::make_unique<BFilePanel>(B_SAVE_PANEL,&target,nullptr,B_FILE_NODE,false,&save);
                fSavePanel->SetSaveText(fSaving.name.c_str()); fSavePanel->Show(); break;
            }
            case kAttachmentSaved: {
                entry_ref directory; const char* name=nullptr;
                if (message->FindRef("directory",&directory)!=B_OK || message->FindString("name",&name)!=B_OK) break;
                BPath path(&directory); path.Append(name);
                BFile file(path.Path(),B_WRITE_ONLY|B_CREATE_FILE|B_ERASE_FILE);
                if (file.InitCheck()!=B_OK || file.Write(fSaving.data.data(),fSaving.data.size())!=static_cast<ssize_t>(fSaving.data.size()) || file.Sync()!=B_OK)
                    throw std::runtime_error("Could not save the attachment. Check free disk space and folder permissions.");
                fStatus->SetText("Attachment saved."); break;
            }
            default: BWindow::MessageReceived(message); break;
        }
    } catch (const std::exception& error) { Error(error.what()); }
}
}

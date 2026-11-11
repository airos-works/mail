// Native integration test: exercise real Interface Kit widgets and the importer.
#include "MailWindow.h"
#include "ComposerWindow.h"
#include "Emoji.h"
#include <Alert.h>
#include <Application.h>
#include <Autolock.h>
#include <Button.h>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <Entry.h>
#include <File.h>
#include <IconUtils.h>
#include <ListView.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Resources.h>
#include <TextControl.h>
#include <TextView.h>
#include <Screen.h>
#include <StringItem.h>
#include <TranslatorRoster.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <unistd.h>

namespace {
class Smoke : public BApplication {
public:
    explicit Smoke(std::string directory)
        : BApplication("application/x-vnd.air-Mail-Smoke"), fDirectory(std::move(directory)) {}
    int result=1;
    void ReadyToRun() override
    {
        try {
            BFile executable("Mail",B_READ_ONLY);
            BResources resources(&executable);
            size_t iconSize=0;
            const void* icon=resources.LoadResource('VICN',"BEOS:ICON",&iconSize);
            check(icon && iconSize>0,"application embeds its HVIF resource");
            for (int size : {16,32,64}) {
                BBitmap bitmap(BRect(0,0,size-1,size-1),B_RGBA32);
                check(BIconUtils::GetVectorIcon(static_cast<const uint8*>(icon),iconSize,&bitmap)==B_OK,"native HVIF renderer accepts the icon");
            }
            fStore=std::make_shared<mail::Store>(fDirectory+"/mail.db");
            mail::Account local; local.name="On this computer"; local.email="pat@example.test";
            fAccount=fStore->saveAccount(local);
            fWindow=new mail::ui::MailWindow(fStore); fWindow->Show();
            BMessage tick('test'); fTimer=std::make_unique<BMessageRunner>(BMessenger(this),&tick,200000);
            std::ofstream out(fDirectory+"/sample.eml");
            out<<"From: Ada <ada@example.test>\nTo: Pat <pat@example.test>\nSubject: Native Mail 🎉\n"
                "Message-ID: <native-smoke@example.test>\nDate: Wed, 21 Oct 2026 11:30:00 +1100\n"
                "Content-Type: text/plain; charset=utf-8\n\nHello from air/OS. Emoji: 🎉 😀 🫠 🫶\n"
                "Smile 😊  Thanks 🙏  Coffee ☕  Launch 🚀\n";
            out.close();
            std::ofstream followup(fDirectory+"/followup.eml");
            followup<<"From: Grace <grace@example.test>\nReply-To: replies@example.test\n"
                "To: Pat <pat@example.test>, team@example.test\nCc: ada@example.test, PAT@example.test\n"
                "Subject: Re: Native Mail 🎉\nMessage-ID: <native-followup@example.test>\n"
                "References: <native-smoke@example.test>\nDate: Wed, 21 Oct 2026 11:31:00 +1100\n"
                "MIME-Version: 1.0\nContent-Type: multipart/mixed; boundary=fixture\n\n"
                "--fixture\nContent-Type: text/plain; charset=utf-8\n\nLatest conversation message.\n"
                "--fixture\nContent-Type: application/octet-stream\nContent-Disposition: attachment; filename=sample.bin\n"
                "Content-Transfer-Encoding: base64\n\nYQB6\n--fixture--\n";
            followup.close();
            entry_ref ref;
            check(get_ref_for_path((fDirectory+"/sample.eml").c_str(),&ref)==B_OK,"fixture reference");
            BMessage refs(B_REFS_RECEIVED); refs.AddRef("refs",&ref);
            check(get_ref_for_path((fDirectory+"/followup.eml").c_str(),&ref)==B_OK,"followup reference");
            refs.AddRef("refs",&ref); fWindow->PostMessage(&refs);
        } catch (const std::exception& error) { fail(error.what()); }
    }
    void MessageReceived(BMessage* message) override
    {
        if (message->what!='test') { BApplication::MessageReceived(message); return; }
        try {
            check(++fTicks<300,"UI test timed out");
            auto rows=fStore->conversations({},2000000000);
            if (fStep==0) {
                if (rows.empty() || fStore->thread(rows[0].id).size()!=2) return;
                check(rows.size()==1,"import creates one conversation");
                check(fWindow->Lock(),"window lock");
                auto* list=dynamic_cast<BListView*>(fWindow->FindView("conversations"));
                if (!list || list->CountItems()!=1) { fWindow->Unlock(); return; }
                list->Select(0); fWindow->Unlock(); fStep=1;
            } else if (fStep==1) {
                check(fWindow->Lock(),"window lock");
                auto* reader=dynamic_cast<BTextView*>(fWindow->FindView("message reader"));
                bool displayed=reader && std::string(reader->Text()).find("Hello from air/OS")!=std::string::npos
                    && std::string(reader->Text()).find("Latest conversation message")!=std::string::npos;
                // The import's last refresh can rebuild the list after it was
                // selected, and the web engine may still be starting: select
                // again and wait.
                if (!displayed && fTicks<100) {
                    auto* rebuilt=dynamic_cast<BListView*>(fWindow->FindView("conversations"));
                    if (rebuilt && rebuilt->CountItems()==1 && rebuilt->CurrentSelection()<0) rebuilt->Select(0);
                    fWindow->Unlock(); return;
                }
                if (displayed) {
                    auto offset=std::string(reader->Text()).find("🎉"); BFont font; reader->GetFontAndColor(offset,&font);
                    bool glyph=false; font.GetHasGlyphs("🎉",1,&glyph,false);
                    check(mail::ui::IsEmojiFont(font) && glyph,"reader uses the embedded font's real emoji glyph");
                }
                fWindow->ResizeTo(1000,650);
                auto* list=dynamic_cast<BListView*>(fWindow->FindView("conversations"));
                bool sizes=list && list->Bounds().Width()>200 && reader && reader->Bounds().Width()>200;
                fWindow->Unlock();
                check(displayed,"selecting a conversation renders imported text"); check(sizes,"layout gives list and reader useful widths");
                fWindow->PostMessage('marc'); fStep=2;
            } else if (fStep==2) {
                if (!rows.empty()) return;
                mail::Query archived; archived.folder="archive";
                auto archive=fStore->conversations(archived,2000000000);
                check(archive.size()==1,"archive command reaches persistent store");
                auto thread=fStore->thread(archive[0].id);
                check(thread.size()==2,"archive retains both members of the conversation");
                for (const auto& m:thread) { check(m.folder=="archive","archive applies to the whole conversation"); fStore->move(m.id,"inbox"); }
                fWindow->PostMessage('mtik'); fStep=3;
            } else if (fStep==3) {
                check(fWindow->Lock(),"window lock");
                auto* search=dynamic_cast<BTextControl*>(fWindow->FindView("search"));
                if (search) search->SetText("notfoundfixture");
                fWindow->Unlock(); check(search,"native search field exists"); fStep=4;
            } else if (fStep==4) {
                check(fWindow->Lock(),"window lock");
                auto* list=dynamic_cast<BListView*>(fWindow->FindView("conversations"));
                bool empty=list && list->CountItems()==0;
                auto* search=dynamic_cast<BTextControl*>(fWindow->FindView("search")); search->SetText("");
                fWindow->Unlock(); check(empty,"native search filters the displayed inbox"); fStep=5;
            } else if (fStep==5) {
                check(fWindow->Lock(),"window lock");
                auto* list=dynamic_cast<BListView*>(fWindow->FindView("conversations"));
                bool restored=list && list->CountItems()==1;
                if (restored) list->Select(0);
                fWindow->Unlock(); check(restored,"clearing search restores the inbox");
                fStep=6;
            } else if (fStep==6) {
                if (const char* path=std::getenv("MAIL_SMOKE_SCREENSHOT")) {
                    BBitmap* bitmap=nullptr;
                    check(BScreen().GetBitmap(&bitmap)==B_OK,"capture native screenshot");
                    BBitmapStream stream(bitmap);
                    BFile file(path,B_WRITE_ONLY|B_CREATE_FILE|B_ERASE_FILE);
                    check(file.InitCheck()==B_OK,"open screenshot output");
                    check(BTranslatorRoster::Default()->Translate(&stream,nullptr,nullptr,&file,B_PNG_FORMAT)==B_OK,"write native screenshot");
                }
                fWindow->PostMessage('mrpl'); fStep=7;
            } else if (fStep==7 || fStep==9 || fStep==11) {
                auto* composer=Composer(); if (!composer) return;
                BAutolock lock(composer); check(lock.IsLocked(),"composer lock");
                auto field=[&](const char* name) {
                    auto* view=dynamic_cast<BTextControl*>(composer->FindView(name));
                    check(view,"composer recipient field exists"); return std::string(view->Text());
                };
                auto drafts=fStore->drafts(); check(drafts.size()==1,"each command creates one editable draft");
                auto d=drafts[0]; fDraft=d.id;
                check(d.account==fAccount && field("compose bcc").empty(),"composer preserves the account and does not invent BCC recipients");
                auto* body=dynamic_cast<BTextView*>(composer->FindView("compose body"));
                check(body && std::string(body->Text()).find("Latest conversation message")!=std::string::npos,
                    "reply and forward quote the latest conversation message");
                if (fStep==11) {
                    check(field("compose to").empty() && field("compose cc").empty(),"forward asks for fresh recipients");
                    check(d.references.empty() && d.subject=="Fwd: Re: Native Mail 🎉","forward starts a new message with the expected subject");
                    check(d.attachments.size()==1 && d.attachments[0].data==std::string("a\0z",3),"forward preserves binary attachment bytes");
                } else {
                    check(field("compose to")==(fStep==7?"replies@example.test":"replies@example.test, team@example.test"),
                        "native reply respects Reply-To and reply-all adds other recipients");
                    check(field("compose cc")== (fStep==7?"":"ada@example.test"),"reply-all excludes the sender's own identity, including case variants");
                    check(d.references.size()==2 && d.references.back()=="<native-followup@example.test>" && d.attachments.empty(),
                        "reply threads to the latest message without copying attachments");
                    check(field("compose subject")=="Re: Native Mail 🎉","reply does not duplicate the subject prefix");
                }
                composer->PostMessage(B_QUIT_REQUESTED); ++fStep;
            } else if (fStep==8 || fStep==10 || fStep==12) {
                if (Composer()) return;
                auto saved=fStore->draft(fDraft); check(saved.id==fDraft,"closing the native composer retains its draft");
                fStore->removeDraft(saved.id,saved.revision);
                { BAutolock lock(fWindow); auto* list=dynamic_cast<BListView*>(fWindow->FindView("conversations"));
                    check(list && list->CountItems()==1,"inbox retains one conversation after closing a draft"); list->Select(0); }
                fWindow->PostMessage(fStep==8?'mrpa':fStep==10?'mfwd':'mtrs'); ++fStep;
            } else if (fStep==13) {
                if (!rows.empty()) return;
                mail::Query trash; trash.folder="trash"; auto trashed=fStore->conversations(trash,2000000000);
                check(trashed.size()==1,"native delete moves the conversation to Trash");
                auto messages=fStore->thread(trashed[0].id);
                check(messages.size()==2,"delete preserves both recoverable messages");
                for (const auto& m:messages) check(m.folder=="trash","delete applies to every member");
                check(fStore->drafts().empty() && fStore->outbox().empty(),"reply and forward review did not queue any delivery");
                // Open the Trash view: there, Delete removes mail for good.
                BAutolock lock(fWindow);
                auto* sidebar=dynamic_cast<BListView*>(fWindow->FindView("mailboxes")); check(sidebar,"sidebar exists");
                int32 index=-1;
                for (int32 i=0;i<sidebar->CountItems();++i)
                    if (auto* item=dynamic_cast<BStringItem*>(sidebar->ItemAt(i))) if (std::string(item->Text())=="Trash") { index=i; break; }
                check(index>=0,"sidebar lists Trash"); sidebar->Select(index); ++fStep;
            } else if (fStep==14 || fStep==16) {
                if (Alert()) return;
                BAutolock lock(fWindow);
                auto* list=dynamic_cast<BListView*>(fWindow->FindView("conversations"));
                if (!list || list->CountItems()!=1) { check(fStep==14,"cancelling keeps the conversation in Trash"); return; }
                auto* item=fWindow->KeyMenuBar()->FindItem('mtrs');
                check(item && std::string(item->Label())=="Delete permanently…","the Trash view offers permanent deletion");
                check(fStore->trashed(0).size()==2,"Trash still holds both messages");
                list->Select(0); fWindow->PostMessage('mtrs'); ++fStep;
            } else if (fStep==15 || fStep==17) {
                auto* alert=Alert(); if (!alert) return;
                BAutolock lock(alert);
                check(std::string(alert->Title())=="Delete permanently","permanent deletion asks first");
                check(alert->DefaultButton()==alert->ButtonAt(0) && std::string(alert->ButtonAt(0)->Label())=="Cancel","the confirmation defaults to Cancel");
                auto text=std::string(alert->TextView()->Text());
                check(text.find("Delete these 2 messages permanently?")!=std::string::npos && text.find("cannot be undone")!=std::string::npos
                    && text.find("Gmail")==std::string::npos,"the confirmation names what is deleted");
                alert->ButtonAt(fStep==15?0:1)->Invoke(); ++fStep;
            } else if (fStep==18) {
                if (Alert() || !fStore->trashed(0).empty()) return;
                mail::Query trash; trash.folder="trash";
                check(fStore->conversations(trash,2000000000).empty() && rows.empty(),"confirmed deletion empties Trash for good");
                BAutolock lock(fWindow);
                auto* list=dynamic_cast<BListView*>(fWindow->FindView("conversations"));
                if (!list || list->CountItems()!=0) return;
                result=0; std::puts("Native Mail smoke passed: HVIF, import, threading, layout, read, archive, search, Reply-To, reply-all, forward attachments, saved drafts, Trash and permanent deletion");
                std::fflush(stdout); PostMessage(B_QUIT_REQUESTED);
            }
        } catch (const std::exception& error) { fail(error.what()); }
    }
private:
    BAlert* Alert()
    {
        for (int32 i=0;auto* window=WindowAt(i);++i) if (auto* alert=dynamic_cast<BAlert*>(window)) return alert;
        return nullptr;
    }
    mail::ui::ComposerWindow* Composer()
    {
        for (int32 i=0;auto* window=WindowAt(i);++i)
            if (auto* composer=dynamic_cast<mail::ui::ComposerWindow*>(window)) return composer;
        return nullptr;
    }
    void check(bool ok,const char* what) { if (!ok) throw std::runtime_error(what); }
    void fail(const char* what) { std::fprintf(stderr,"FAIL: %s\n",what); PostMessage(B_QUIT_REQUESTED); }
    std::string fDirectory;
    std::shared_ptr<mail::Store> fStore;
    mail::ui::MailWindow* fWindow=nullptr;
    std::unique_ptr<BMessageRunner> fTimer;
    int fStep=0,fTicks=0;
    mail::Id fAccount=0,fDraft=0;
};
}
int main()
{
    char directory[]="/tmp/mail-native-XXXXXX";
    if (!mkdtemp(directory)) return 2;
    int result;
    { Smoke smoke(directory); smoke.Run(); result=smoke.result; }
    // Remove the known fixtures individually: old Haiku BFS directory iterators
    // can skip entries when recursive deletion mutates the directory mid-walk.
    for (const char* file : {"sample.eml","followup.eml","mail.db-wal","mail.db-shm","mail.db"})
        unlink((std::string(directory)+"/"+file).c_str());
    rmdir(directory);
    return result;
}

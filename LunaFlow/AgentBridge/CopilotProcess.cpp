#include "AgentBridge/CopilotProcess.h"
#include <stdexcept>

namespace LunaFlow {
namespace {
struct Handle {HANDLE h=nullptr;~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}HANDLE* Put(){return &h;}};
std::wstring Quote(const std::filesystem::path& p){auto s=p.wstring();if(s.find(L'"')!=std::wstring::npos)throw std::runtime_error("Invalid SDK file path.");return L"\""+s+L"\"";}
}
std::filesystem::path CopilotProjectRoot(){
    wchar_t file[32768]{};GetModuleFileNameW(nullptr,file,32768);auto root=std::filesystem::path(file).parent_path();
    for(int i=0;i<6&&!root.empty();++i){if(std::filesystem::is_regular_file(root/L"agent_service/copilot_client/CopilotConnection.csproj"))return root;auto parent=root.parent_path();if(parent==root)break;root=parent;}
    throw std::runtime_error("The LunaFlow agent_service folder was not found above the executable folder.");
}
void CopilotProcess::Publish(Json event){std::lock_guard<std::mutex> lock(mutex_);if(events_.size()<256)events_.push_back(std::move(event));}
std::vector<Json> CopilotProcess::Poll(){std::lock_guard<std::mutex> lock(mutex_);std::vector<Json> result;while(!events_.empty()){result.push_back(std::move(events_.front()));events_.pop_front();}return result;}
void CopilotProcess::Cancel(){
    cancelled_=true;
    {std::lock_guard<std::mutex> lock(mutex_);if(process_)TerminateProcess(process_,3);}
    if(worker_.joinable())worker_.join();busy_=false;
}
void CopilotProcess::Submit(const std::filesystem::path& root,const Json& request,const std::filesystem::path& settings){
    if(busy_)throw std::runtime_error("Copilot is already working.");
    Cancel();Poll();cancelled_=false;
    const auto binary=root/L"agent_service/copilot_client/bin/Release/net10.0/CopilotConnection.exe";
    const auto config=settings.empty()?root/L"agent_service/copilot_client/settings.local.json":settings;
    if(!std::filesystem::is_regular_file(binary)||!std::filesystem::is_regular_file(config))throw std::runtime_error("Copilot SDK client/config is missing. Run Build-CopilotClient.ps1 in the project folder.");
    auto input=request.dump();if(input.size()>1024*1024)throw std::runtime_error("This scene exceeds the 1 MB AI request budget.");input+='\n';busy_=true;
    worker_=std::thread([this,binary,config,input=std::move(input),request]{
        Handle ownedProcess;
        const auto id=request.at("requestId"),session=request.at("sessionId");
        auto fail=[&](const std::string& message){Publish({{"requestId",id},{"sessionId",session},{"status","failed"},{"message",message}});};
        try{
            SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};Handle read,write,childRead,childWrite;
            if(!CreatePipe(childRead.Put(),write.Put(),&security,0)||!CreatePipe(read.Put(),childWrite.Put(),&security,0))throw std::runtime_error("Cannot create SDK pipes.");
            SetHandleInformation(write.h,HANDLE_FLAG_INHERIT,0);SetHandleInformation(read.h,HANDLE_FLAG_INHERIT,0);
            STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
            startup.hStdInput=childRead.h;startup.hStdOutput=childWrite.h;startup.hStdError=childWrite.h;
            PROCESS_INFORMATION process{};
            auto command=Quote(binary)+L" --worker --config "+Quote(config);
            if(!CreateProcessW(binary.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,binary.parent_path().c_str(),&startup,&process))throw std::runtime_error("Cannot start the Copilot SDK client. Install .NET 10 Runtime if it is missing.");
            ownedProcess.h=process.hProcess;Handle thread;thread.h=process.hThread;
            {std::lock_guard<std::mutex> lock(mutex_);process_=ownedProcess.h;if(cancelled_)TerminateProcess(process_,3);}
            CloseHandle(childRead.h);childRead.h=nullptr;CloseHandle(childWrite.h);childWrite.h=nullptr;
            DWORD written=0;size_t offset=0;
            while(offset<input.size()){
                if(!WriteFile(write.h,input.data()+offset,static_cast<DWORD>(input.size()-offset),&written,nullptr)||written==0)throw std::runtime_error("SDK request pipe closed.");offset+=written;
            }
            CloseHandle(write.h);write.h=nullptr; // One request per owned worker; EOF permits clean shutdown.
            std::string line;char chunk[4096];DWORD count=0;bool terminal=false;
            while(!cancelled_&&ReadFile(read.h,chunk,sizeof(chunk),&count,nullptr)&&count){
                for(DWORD i=0;i<count;++i){
                    if(chunk[i]=='\n'){
                        if(!line.empty()&&line.back()=='\r')line.pop_back();
                        auto event=Json::parse(line,nullptr,false);line.clear();
                        if(event.is_discarded()||!event.is_object()||event.value("requestId",Json())!=id||event.value("sessionId",Json())!=session)continue;
                        const auto state=event.value("status",std::string{});terminal|=state=="completed"||state=="failed";Publish(std::move(event));
                    }else {line+=chunk[i];if(line.size()>2*1024*1024)throw std::runtime_error("SDK output exceeds its size limit.");}
                }
            }
            if(!terminal&&!cancelled_)fail("SDK client exited without a result. Check .NET 10, sign-in and API permissions.");
            WaitForSingleObject(ownedProcess.h,2000);
            {std::lock_guard<std::mutex> lock(mutex_);if(WaitForSingleObject(ownedProcess.h,0)==WAIT_TIMEOUT)TerminateProcess(ownedProcess.h,3);process_=nullptr;}
        }catch(const std::exception& e){
            {std::lock_guard<std::mutex> lock(mutex_);if(process_){TerminateProcess(process_,3);process_=nullptr;}}
            if(!cancelled_)fail(e.what());
        }
        busy_=false;
    });
}
}

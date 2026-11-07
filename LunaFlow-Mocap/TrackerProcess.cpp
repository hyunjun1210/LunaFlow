#include "TrackerProcess.h"
#include <windows.h>
#include <stdexcept>

namespace LunaFlow::Mocap {
namespace {
struct Handle {
    HANDLE value=nullptr;
    ~Handle(){Close();}
    void Close(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=nullptr;}
};
std::wstring Quote(const std::filesystem::path& path) {
    const auto text=path.wstring();
    if(text.empty()||text.find(L'"')!=std::wstring::npos||text.back()==L'\\')throw std::runtime_error("Invalid mocap executable/file path.");
    return L"\""+text+L"\"";
}
std::string WinError(const char* operation) {
    return std::string(operation)+" (Windows error "+std::to_string(GetLastError())+")";
}
}
TrackerProcess::~TrackerProcess() {RequestStop();if(worker_.joinable())worker_.join();}
void TrackerProcess::Publish(Event event) {
    event.received=Clock();
    std::lock_guard<std::mutex> lock(mutex_);
    if(events_.size()>=120) {
        // Never silently drop a take's samples: stop this capture with a visible error.
        stop_=true;events_.clear();events_.push_back({{},"error","Mocap queue overflow: editor was stalled. Take ended.",event.received});return;
    }
    events_.push_back(std::move(event));
}
std::vector<Event> TrackerProcess::Poll() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Event> result;
    while(!events_.empty()){result.push_back(std::move(events_.front()));events_.pop_front();}
    return result;
}
void TrackerProcess::Start(const std::filesystem::path& python,const std::filesystem::path& script,
    const std::filesystem::path& model,int camera) {
    if(Busy())throw std::runtime_error("Camera worker is already running.");
    if(worker_.joinable())worker_.join();
    for(const auto& path:{python,script,model})
        if(!std::filesystem::is_regular_file(path))throw std::runtime_error("Mocap runtime/model missing. Run LunaFlow-Mocap\\Setup.ps1.");
    if(camera<0||camera>15)throw std::runtime_error("Camera index must be 0..15.");
    const auto command=Quote(python)+L" -I -u "+Quote(script)+L" --model "+Quote(model)+L" --camera "+std::to_wstring(camera);
    Poll();stop_=false;busy_=true;
    try { worker_=std::thread([this,python,command] {
        Handle process,job;
        try {
            SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
            Handle input,childInput,output,childOutput;
            if(!CreatePipe(&childInput.value,&input.value,&security,0)||!CreatePipe(&output.value,&childOutput.value,&security,0)||
                !SetHandleInformation(input.value,HANDLE_FLAG_INHERIT,0)||!SetHandleInformation(output.value,HANDLE_FLAG_INHERIT,0))
                throw std::runtime_error(WinError("Cannot create mocap pipes"));
            job.value=CreateJobObjectW(nullptr,nullptr);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if(!job.value||!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))
                throw std::runtime_error(WinError("Cannot create mocap process job"));
            // Explicit inheritance list prevents leaking unrelated editor handles.
            SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);
            std::vector<unsigned char> storage(size);
            auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
            if(!InitializeProcThreadAttributeList(attributes,1,0,&size))throw std::runtime_error(WinError("Cannot initialize mocap startup"));
            struct Attributes {LPPROC_THREAD_ATTRIBUTE_LIST p;~Attributes(){DeleteProcThreadAttributeList(p);}} cleanup{attributes};
            HANDLE inherited[]={childInput.value,childOutput.value};
            if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr))
                throw std::runtime_error(WinError("Cannot restrict mocap handles"));
            STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes;
            startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
            startup.StartupInfo.hStdInput=childInput.value;startup.StartupInfo.hStdOutput=childOutput.value;startup.StartupInfo.hStdError=childOutput.value;
            PROCESS_INFORMATION info{};auto mutableCommand=command;
            if(!CreateProcessW(python.c_str(),mutableCommand.data(),nullptr,nullptr,TRUE,
                CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,python.parent_path().c_str(),&startup.StartupInfo,&info))
                throw std::runtime_error(WinError("Cannot launch mocap Python"));
            process.value=info.hProcess;Handle thread;thread.value=info.hThread;
            if(!AssignProcessToJobObject(job.value,process.value))throw std::runtime_error(WinError("Cannot own mocap process"));
            if(ResumeThread(thread.value)==static_cast<DWORD>(-1))throw std::runtime_error(WinError("Cannot resume mocap process"));
            childInput.Close();childOutput.Close();
            std::string line,diagnostics;bool ready=false,terminal=false,sentStop=false;
            double last=Clock(),stopAt=0;uint64_t seq=0;double stamp=-1;
            for(;;) {
                const double now=Clock();
                if(stop_&&!sentStop) {
                    DWORD written=0;const char message[]="stop\n";
                    if(!WriteFile(input.value,message,sizeof(message)-1,&written,nullptr)&&WaitForSingleObject(process.value,0)==WAIT_TIMEOUT)
                        Publish({{},"error","Camera stop pipe closed; waiting for owned worker cleanup."});
                    input.Close();sentStop=true;stopAt=now;
                }
                if(sentStop&&now-stopAt>3) {
                    TerminateJobObject(job.value,3);WaitForSingleObject(process.value,1000);
                    Publish({{},"error","Camera worker did not stop in 3s; owned process terminated and camera handles released."});terminal=true;break;
                }
                if(!sentStop&&now-last>(ready?3.:60.))
                    throw std::runtime_error("Camera/model worker timeout. Check camera permissions/device and runtime installation.");
                DWORD available=0;
                if(!PeekNamedPipe(output.value,nullptr,0,nullptr,&available,nullptr)) {
                    if(GetLastError()==ERROR_BROKEN_PIPE)break;
                    throw std::runtime_error(WinError("Mocap pipe read failed"));
                }
                if(available) {
                    char chunk[4096];DWORD read=0;
                    if(!ReadFile(output.value,chunk,std::min<DWORD>(available,sizeof(chunk)),&read,nullptr))
                        throw std::runtime_error(WinError("Mocap pipe closed"));
                    for(DWORD i=0;i<read;++i) {
                        if(chunk[i]!='\n') {line+=chunk[i];if(line.size()>32768)throw std::runtime_error("Mocap output line too large.");continue;}
                        if(!line.empty()&&line.back()=='\r')line.pop_back();
                        if(!line.empty()&&line.front()=='{') {
                            auto event=ParseEvent(line);last=Clock();
                            if(event.frame) {
                                if(!ready||event.frame->sequence<=seq||event.frame->time<=stamp)throw std::runtime_error("Mocap stream order violation.");
                                seq=event.frame->sequence;stamp=event.frame->time;
                            }else {ready|=event.state=="ready";terminal|=event.state=="error"||event.state=="stopped";}
                            Publish(std::move(event));
                        }else if(!line.empty()) {diagnostics+=line+"\n";if(diagnostics.size()>2048)diagnostics.erase(0,diagnostics.size()-2048);}
                        line.clear();
                    }
                }else if(WaitForSingleObject(process.value,0)==WAIT_OBJECT_0)break;
                else std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if(!terminal&&!stop_)throw std::runtime_error("Camera worker exited unexpectedly. "+diagnostics);
            if(WaitForSingleObject(process.value,1000)==WAIT_TIMEOUT)TerminateJobObject(job.value,3);
        }catch(const std::exception& e) {
            if(process.value){TerminateProcess(process.value,3);WaitForSingleObject(process.value,1000);}
            Publish({{},"error",e.what()});
        }
        // Release the job before exposing idle, including possible Python descendants.
        job.Close();process.Close();busy_=false;
    }); }catch(...) {busy_=false;throw;}
}
SetupProcess::~SetupProcess() {Cancel();if(worker_.joinable())worker_.join();}
void SetupProcess::Append(std::string line) {
    while(!line.empty()&&(line.back()==' '||line.back()=='\t'))line.pop_back();
    if(line.empty())return;
    std::lock_guard<std::mutex> lock(mutex_);
    if(log_.size()>=200)log_.pop_front();
    log_.push_back(std::move(line));
}
std::vector<std::string> SetupProcess::Log() {
    std::lock_guard<std::mutex> lock(mutex_);
    return {log_.begin(),log_.end()};
}
void SetupProcess::Start(const std::filesystem::path& script) {
    if(Busy())throw std::runtime_error("Mocap setup is already running.");
    if(worker_.joinable())worker_.join();
    if(!std::filesystem::is_regular_file(script))throw std::runtime_error("Setup.ps1 not found in the mocap folder.");
    wchar_t system[MAX_PATH]{};
    if(!GetSystemDirectoryW(system,MAX_PATH))throw std::runtime_error(WinError("Cannot locate Windows PowerShell"));
    const std::filesystem::path shell=std::filesystem::path(system)/L"WindowsPowerShell"/L"v1.0"/L"powershell.exe";
    const auto command=Quote(shell)+L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "+Quote(script)+L" -SmokeTest";
    {std::lock_guard<std::mutex> lock(mutex_);log_.clear();}
    cancel_=false;exitCode_=-1;busy_=true;started_=true;
    try { worker_=std::thread([this,shell,command,folder=script.parent_path()] {
        int code=1;
        try {
            SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
            Handle output,childOutput,childInput;
            if(!CreatePipe(&output.value,&childOutput.value,&security,0)||!SetHandleInformation(output.value,HANDLE_FLAG_INHERIT,0))
                throw std::runtime_error(WinError("Cannot create setup pipe"));
            childInput.value=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);
            if(childInput.value==INVALID_HANDLE_VALUE)throw std::runtime_error(WinError("Cannot open setup input"));
            Handle job;job.value=CreateJobObjectW(nullptr,nullptr);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if(!job.value||!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))
                throw std::runtime_error(WinError("Cannot create setup job"));
            SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);
            std::vector<unsigned char> storage(size);
            auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
            if(!InitializeProcThreadAttributeList(attributes,1,0,&size))throw std::runtime_error(WinError("Cannot initialize setup startup"));
            struct Attributes {LPPROC_THREAD_ATTRIBUTE_LIST p;~Attributes(){DeleteProcThreadAttributeList(p);}} cleanup{attributes};
            HANDLE inherited[]={childInput.value,childOutput.value};
            if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr))
                throw std::runtime_error(WinError("Cannot restrict setup handles"));
            STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes;
            startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
            startup.StartupInfo.hStdInput=childInput.value;startup.StartupInfo.hStdOutput=childOutput.value;startup.StartupInfo.hStdError=childOutput.value;
            PROCESS_INFORMATION info{};auto mutableCommand=command;Handle process,thread;
            if(!CreateProcessW(shell.c_str(),mutableCommand.data(),nullptr,nullptr,TRUE,
                CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,folder.c_str(),&startup.StartupInfo,&info))
                throw std::runtime_error(WinError("Cannot launch Windows PowerShell"));
            process.value=info.hProcess;thread.value=info.hThread;
            if(!AssignProcessToJobObject(job.value,process.value))throw std::runtime_error(WinError("Cannot own setup process"));
            if(ResumeThread(thread.value)==static_cast<DWORD>(-1))throw std::runtime_error(WinError("Cannot resume setup process"));
            childInput.Close();childOutput.Close();
            std::string line;
            for(;;) {
                if(cancel_) {
                    TerminateJobObject(job.value,2);WaitForSingleObject(process.value,2000);
                    Append("Setup cancelled.");code=2;break;
                }
                DWORD available=0;
                if(!PeekNamedPipe(output.value,nullptr,0,nullptr,&available,nullptr)) {
                    if(GetLastError()!=ERROR_BROKEN_PIPE)throw std::runtime_error(WinError("Setup output read failed"));
                    WaitForSingleObject(process.value,INFINITE);
                    DWORD status=1;GetExitCodeProcess(process.value,&status);code=static_cast<int>(status);break;
                }
                if(!available){std::this_thread::sleep_for(std::chrono::milliseconds(30));continue;}
                char chunk[4096];DWORD read=0;
                if(!ReadFile(output.value,chunk,std::min<DWORD>(available,sizeof(chunk)),&read,nullptr))continue;
                for(DWORD i=0;i<read;++i) {
                    // pip redraws progress with carriage returns; every redraw becomes the latest line.
                    if(chunk[i]=='\n'||chunk[i]=='\r'){Append(std::move(line));line.clear();}
                    else if(line.size()<1024)line+=chunk[i];
                }
            }
            Append(std::move(line));
        }catch(const std::exception& e) {
            Append(e.what());code=1;
        }
        exitCode_=code;busy_=false;
    }); }catch(...) {busy_=false;exitCode_=1;throw;}
}
}

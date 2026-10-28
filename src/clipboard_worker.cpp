#include "gvn/clipboard_worker.h"
#include "gvn/edit.h"
namespace gvn {
namespace {
Rect copy_area(const ClipboardCopyRequest &r){return intersect(r.selection,{0,0,int(r.image->width),int(r.image->height)});}
}
PreparedClipboardImage prepare_clipboard_copy(const ClipboardCopyRequest &r,const CancelCheck &cancel){
    if(r.selection.empty())return prepare_clipboard_image(*r.image,r.frame,{},&r.metadata,cancel);
    const auto area=copy_area(r);if(area.empty())throw std::invalid_argument("Empty clipboard selection");
    auto selected=new_image(area.w,area.h);const auto &pixels=r.image->frames.at(r.frame).pixels;
    for(int y=0;y<area.h;++y){check_cancelled(cancel);
        for(int x=0;x<area.w;++x)selected.frames[0].pixels[size_t(y)*area.w+x]=r.selection.contains(x+area.x,y+area.y)
            ?pixels[size_t(y+area.y)*r.image->width+x+area.x]:Pixel{};
    }
    return prepare_clipboard_image(selected,0,SelectionOrigin{r.selection.x,r.selection.y,r.image->width,r.image->height},&r.metadata,cancel);
}
ClipboardCopyWorker::ClipboardCopyWorker(std::function<void()> callback,Encoder encode):
    notify(std::move(callback)),encoder(encode?std::move(encode):Encoder(prepare_clipboard_copy)),thread([this](std::stop_token stop){run(stop);}){}
ClipboardCopyWorker::~ClipboardCopyWorker(){thread.request_stop();++epoch;ready.notify_all();if(thread.joinable())thread.join();}
uint64_t ClipboardCopyWorker::request(ClipboardCopyRequest request){
    if(!request.image||request.frame>=request.image->frames.size())throw std::invalid_argument("Invalid copy request");
    std::lock_guard lock(mutex);const auto id=++epoch;pending=Job{std::move(request),id};result.reset();ready.notify_all();return id;
}
void ClipboardCopyWorker::cancel(){std::lock_guard lock(mutex);++epoch;pending.reset();result.reset();ready.notify_all();}
std::optional<ClipboardCopyResult> ClipboardCopyWorker::take(){std::lock_guard lock(mutex);auto out=std::move(result);result.reset();return out;}
bool ClipboardCopyWorker::wait_idle(std::chrono::milliseconds timeout){std::unique_lock lock(mutex);return ready.wait_for(lock,timeout,[&]{return !working&&!pending;});}
void ClipboardCopyWorker::run(std::stop_token stop){
    while(!stop.stop_requested()){
        std::optional<Job> job;
        {std::unique_lock lock(mutex);ready.wait(lock,stop,[&]{return pending.has_value();});if(stop.stop_requested())break;
            job=std::move(pending);pending.reset();working=true;}
        const CancelCheck cancelled=[&]{return stop.stop_requested()||epoch.load()!=job->sequence;};
        ClipboardCopyResult completed;completed.sequence=job->sequence;
        try{
            const auto &r=job->request;const auto area=r.selection.empty()?Rect{0,0,int(r.image->width),int(r.image->height)}:copy_area(r);
            completed.size={area.w,area.h};completed.prepared=encoder(r,cancelled);
        }catch(const std::exception &e){completed.error=e.what();}catch(...){completed.error="Clipboard preparation failed";}
        bool post{};
        {std::lock_guard lock(mutex);if(!cancelled()){result=std::move(completed);post=true;}}
        job.reset(); // Releasing a large retired image must not hold the UI request mutex.
        if(post&&notify)try{notify();}catch(...){}
        {std::lock_guard lock(mutex);working=false;}
        ready.notify_all();
    }
}
}

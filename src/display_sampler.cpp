#include "gvn/display_sampler.h"

namespace gvn {
DisplaySampler::DisplaySampler(std::function<void()> callback):notify(std::move(callback)),
    thread([this](std::stop_token stop){run(stop);}) {}
DisplaySampler::~DisplaySampler(){thread.request_stop();++epoch;ready.notify_all();if(thread.joinable())thread.join();}
uint64_t DisplaySampler::request(DisplayRequest value) {
    if(!value.image||value.frame>=value.image->frames.size()||value.method<DisplaySimple||value.method>DisplayLanczos3)
        throw std::invalid_argument("Invalid asynchronous display request");
    std::lock_guard lock(mutex);
    if(latest&&latest->same(value)&&(working||pending||result))return epoch.load();
    const auto sequence=++epoch;latest=value;pending=Job{std::move(value),sequence};result.reset();
    ready.notify_all();return sequence;
}
void DisplaySampler::cancel(){std::lock_guard lock(mutex);++epoch;pending.reset();result.reset();latest.reset();notified=false;ready.notify_all();}
std::optional<DisplaySample> DisplaySampler::take(){std::lock_guard lock(mutex);notified=false;auto value=std::move(result);result.reset();return value;}
bool DisplaySampler::matches(const DisplayRequest &value) const {std::lock_guard lock(mutex);return latest&&latest->same(value);}
bool DisplaySampler::idle() const {std::lock_guard lock(mutex);return !working&&!pending;}
bool DisplaySampler::wait_idle(std::chrono::milliseconds timeout){std::unique_lock lock(mutex);return ready.wait_for(lock,timeout,[&]{return !working&&!pending;});}
void DisplaySampler::publish(DisplaySample sample,std::stop_token stop) {
    bool post{};
    {std::lock_guard lock(mutex);if(stop.stop_requested()||sample.sequence!=epoch.load())return;
        result=std::move(sample);post=!notified;notified=true;}
    if(post&&notify)notify();
}
void DisplaySampler::run(std::stop_token stop) {
    while(!stop.stop_requested()){
        std::optional<Job> job;
        {std::unique_lock lock(mutex);ready.wait(lock,stop,[&]{return pending.has_value();});
            if(stop.stop_requested())break;job=std::move(pending);pending.reset();working=true;}
        const auto sequence=job->sequence;
        const std::function<bool()> cancelled=[&]{return stop.stop_requested()||epoch.load()!=sequence;};
        try {
            const auto &request=job->request;const auto &image=*request.image;
            if(request.method!=DisplayLinear){
                auto preview=resample_rgba_display(image.frames.at(request.frame).pixels,image.width,image.height,
                    request.view,DisplayLinear,cancelled);
                publish({request,std::move(preview),DisplayLinear,sequence,false},stop);
            }
            if(!cancelled()){
                auto tile=resample_rgba_display(image.frames.at(request.frame).pixels,image.width,image.height,
                    request.view,request.method,cancelled);
                publish({request,std::move(tile),request.method,sequence,false},stop);
            }
        }catch(...){if(!cancelled())publish({job->request,{},job->request.method,sequence,true},stop);}
        job.reset();
        {std::lock_guard lock(mutex);working=false;}ready.notify_all();
    }
}
} // namespace gvn

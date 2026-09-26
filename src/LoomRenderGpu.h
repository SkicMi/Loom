#pragma once
//=============================================================================================
// GPU POGON RENDERA: posao iz RenderSessiona (LoomRender.h) na karticu, iz niti koja crta.
//
// Editor i loom-render ga zovu dvaput po kadru:
//
//   beforeFrame()   IZVAN kadra: preuzme novi posao (scena na karticu), procita sliku za prikaz
//                   koju je proslo kadar zatrazio, a kad je render gotov procita film i vrati ga
//   inFrame()       UNUTAR kadra (poslije beginFrame, prije beginPass): onoliko redaka-uzoraka
//                   koliko stane u proracun vremena, i po potrebi sliku za prikaz
//
// PRORACUN. Editor mora ostati odziv: posao po kadru se prilagodjava tako da render kadru DODA
// oko targetSeconds. Mjeri se trajanje kadra (od jednog beforeFrame do sljedeceg, s cekanjem na
// karticu) i od njega oduzme trajanje kadra BEZ rendera (klizni prosjek dok pogon miruje). Bez
// toga bi spor pogled (llvmpipe: 70 ms po kadru) srezao render na jedan redak po kadru.
// loom-render nema prozor, pa mu je proracun velik - ali i dalje ogranicen, jer dispatch dulji od
// ~2 s Windows ubije kao zaglavljen (TDR).
//=============================================================================================
#include "LoomRender.h"

#include "Core/LoomInitializer.h"
#include <TracerGpu/GpuTracer.h>

#include <algorithm>
#include <chrono>
#include <memory>

namespace Loom{

class GpuRenderDriver{
public:
    explicit GpuRenderDriver(LoomInitializer& loom, double targetSeconds = 0.016) : loom(loom), target(targetSeconds){}

    bool busy() const {return tracer != nullptr;}

    void beforeFrame(RenderSession& session){
        const auto now = std::chrono::steady_clock::now();
        const double frameSeconds = std::chrono::duration<double>(now - lastFrame).count();
        lastFrame = now;
        if(!tracer && frameSeconds > 0.0 && frameSeconds < 1.0)
            idleSeconds = idleSeconds <= 0.0 ? frameSeconds : idleSeconds * 0.9 + frameSeconds * 0.1;
        try{
            if(tracer && session.cancelled()){ loom.waitIdle(); tracer.reset(); }
            if(!tracer){
                if(!session.takeGpuJob(job)) return;
                if(!pipelines) pipelines = std::make_unique<TracerGpu::Pipelines>(loom);
                tracer = std::make_unique<TracerGpu::GpuTracer>(loom, *pipelines, job.scene, job.settings);
                rows = std::max(1u, tracer->height() / 16);
                previewRequested = false;
                lastPreview = now;
                started = now;
                return;
            }
            //Proracun: posao po kadru raste dok kadar ne dosegne cilj, i pada cim ga prijedje
            if(sentLastFrame > 0 && frameSeconds > 0.0){
                const double renderShare = std::max(frameSeconds - idleSeconds, 1e-4);
                const double scale = std::clamp(target / renderShare, 0.5, 1.5);
                rows = uint32_t(std::clamp(double(rows) * scale, 1.0, double(tracer->height()) * 64.0));
            }
            if(previewRequested){
                previewRequested = false;
                session.publishPreview(tracer->readDisplay(), tracer->width(), tracer->height());
            }
            if(tracer->finished()){
                Tracer::Frame frame = tracer->readFrame(false);
                tracer.reset();
                session.finishGpuJob(job, std::move(frame));
            }
        }catch(const std::exception& failure){
            tracer.reset();
            session.failGpuJob(job, failure.what());
        }
    }

    void inFrame(RenderSession& session){
        sentLastFrame = 0;
        if(!tracer) return;
        try{
            sentLastFrame = tracer->record(rows);
            session.gpuProgress(job, tracer->samplesDone());
            const auto now = std::chrono::steady_clock::now();
            if(tracer->finished() || now - lastPreview > std::chrono::milliseconds(previewMilliseconds)){
                lastPreview = now;
                TracerGpu::DisplayOptions options;
                options.backdrop = backdropFor(job.options, job.plateLoaded);
                options.view = job.options.view;
                options.exposure = job.options.exposure;
                options.checker = true;
                //Pregled kao gotov kadar: filtar suma i post na kartici (isti kao displayImage)
                options.denoise = job.options.denoise;
                options.post = job.options.post;
                options.grainSeed = uint32_t(std::llround(job.frame)) * 7919u + job.options.post.grainSeed;
                tracer->recordDisplay(options);
                previewRequested = true;
            }
        }catch(const std::exception& failure){
            tracer.reset();
            session.failGpuJob(job, failure.what());
        }
    }

    //Koliko cesto se slika za prikaz cita natrag (svako citanje ceka karticu)
    int previewMilliseconds = 500;

private:
    LoomInitializer& loom;
    double target;
    double idleSeconds = 0.0;
    std::unique_ptr<TracerGpu::Pipelines> pipelines;
    std::unique_ptr<TracerGpu::GpuTracer> tracer;
    RenderSession::GpuJob job;
    uint32_t rows = 1, sentLastFrame = 0;
    bool previewRequested = false;
    std::chrono::steady_clock::time_point lastFrame = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastPreview, started;
};

}

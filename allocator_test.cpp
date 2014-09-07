#include <vector>
#include <set>
#include <stdio.h>
#include <assert.h>
#include <windows.h>
#include <comutil.h>
#include "DeckLinkAPI_h.h"

//#define DISABLE_CUSTOM_ALLOCATOR
//#define DISABLE_INPUT_CALLBACK
const BMDDisplayMode g_mode = bmdModePAL;
const size_t g_frame_size = 900000; // (bmdMode4kDCI25 frame_size=18685440)
const size_t g_frame_count = 61;

//=====================================================================================================================
class InputCallback : public IDeckLinkInputCallback
{
    LONG volatile ref_count;
    LONG volatile frame_count, signal_frame_count;

public:
    InputCallback() : ref_count(1), frame_count(0), signal_frame_count(0)  {}

    ~InputCallback()
    {
        printf( "InputCallback::~InputCallback - ref_count=%ld, frame_count=%ld(%ld)\n",
                                                        (long)ref_count, (long)frame_count, (long)signal_frame_count );
    }

    // overrides from IDeckLinkInputCallback
    virtual HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(
                                                        BMDVideoInputFormatChangedEvents notificationEvents,
                                                        IDeckLinkDisplayMode* newDisplayMode,
                                                        BMDDetectedVideoInputFormatFlags detectedSignalFlags
                                                        );

    virtual HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(
                                                        IDeckLinkVideoInputFrame* videoFrame,
                                                        IDeckLinkAudioInputPacket* audioPacket
                                                        );

    // overrides from IUnknown
    virtual HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void** pp );

    virtual ULONG STDMETHODCALLTYPE AddRef(void);
    virtual ULONG STDMETHODCALLTYPE Release(void);
};

//---------------------------------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE InputCallback::VideoInputFormatChanged(
                                                            BMDVideoInputFormatChangedEvents notificationEvents,
                                                            IDeckLinkDisplayMode* newDisplayMode,
                                                            BMDDetectedVideoInputFormatFlags detectedSignalFlags
                                                            )
{
    return S_OK;
}

//---------------------------------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE InputCallback::VideoInputFrameArrived(
                                                            IDeckLinkVideoInputFrame* videoFrame,
                                                            IDeckLinkAudioInputPacket* audioPacket
                                                            )
{
    if( videoFrame != 0 )
    {
        InterlockedIncrement( &frame_count );

        if( ( videoFrame->GetFlags() & bmdFrameHasNoInputSource ) == 0 )
        {
            InterlockedIncrement( &signal_frame_count );
        }
    }

    return S_OK;
}

//---------------------------------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE InputCallback::QueryInterface( REFIID riid, void** pp )
{
    if( IsEqualGUID( riid, IID_IDeckLinkInputCallback ) )
    {
        int cnt = InterlockedIncrement( &ref_count );
        printf( "InputCallback::QueryInterface(IDeckLinkInputCallback) - ref_count=%d\n", cnt );
        *pp = static_cast<IDeckLinkInputCallback*>(this);
        return S_OK;
    }

    if( IsEqualGUID( riid, IID_IUnknown ) )
    {
        int cnt = InterlockedIncrement( &ref_count );
        printf( "InputCallback::QueryInterface(IUnknown) - ref_count=%d\n", cnt );
        *pp = static_cast<IUnknown*>(this);
        return S_OK;
    }

    return E_NOINTERFACE;
}

//---------------------------------------------------------------------------------------------------------------------
ULONG STDMETHODCALLTYPE InputCallback::AddRef(void)
{
    int cnt = InterlockedIncrement( &ref_count );
    printf( "InputCallback::AddRef (ref_count=%d)\n", cnt );
    return cnt;
}

//---------------------------------------------------------------------------------------------------------------------
ULONG STDMETHODCALLTYPE InputCallback::Release(void)
{
    int cnt = InterlockedDecrement( &ref_count );
    printf( "InputCallback::Release (ref_count=%d)\n", cnt );
    return cnt;
}

//=====================================================================================================================
class Alloc: public IDeckLinkMemoryAllocator
{
    LONG volatile ref_count;

    CRITICAL_SECTION  buffers_lock;
    std::vector<char*>  free_buffers;
    std::set<char*>  reserved_buffers;

    Alloc() : ref_count(1)
    {
        printf("Alloc::Alloc (ref_count=1)\n");
        InitializeCriticalSection(&buffers_lock);

        free_buffers.reserve(g_frame_count);

        for( size_t n = g_frame_count; n > 0; --n )
        {
            char* ptr;

            for(;;)
            {
                ptr = new char[ g_frame_size + 256 ];

                if( (uintptr_t)ptr % 16 == 0 )
                {
                    break;
                }

                delete[] ptr;
            }

            free_buffers.push_back(ptr);
        }
    }

    ~Alloc()
    {
        DeleteCriticalSection(&buffers_lock);

        printf(  "Alloc::~Alloc free_buf_count=%lu, alloc_buf_count=%lu\n",
                    unsigned long( free_buffers.size() ), unsigned long( reserved_buffers.size() )  );

        assert( reserved_buffers.empty() );

        for( std::vector<char*>::iterator it = free_buffers.begin(); it != free_buffers.end(); ++it )
        {
            delete[] *it;
        }

        for( std::set<char*>::iterator it = reserved_buffers.begin(); it != reserved_buffers.end(); ++it )
        {
            delete[] *it;
        }
    }

public:
    static IDeckLinkMemoryAllocator* CreateInstance()  { return new Alloc(); }

    virtual ULONG STDMETHODCALLTYPE AddRef()
    {
        int cnt = InterlockedIncrement( &ref_count );
        printf( "Alloc::AddRef (ref_count=%d)\n", cnt );
        return cnt;
    }

    virtual ULONG STDMETHODCALLTYPE Release()
    {
        int cnt = InterlockedDecrement( &ref_count );
        printf( "Alloc::Release (ref_count=%d)\n", cnt );

        if( cnt <= 0 )
        {
            delete this;
        }

        return cnt;
    }

    virtual HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void** pp )
    {
        if( IsEqualGUID( riid, IID_IDeckLinkMemoryAllocator ) )
        {
            int cnt = InterlockedIncrement( &ref_count );
            printf( "Alloc::QueryInterface(IDeckLinkInputCallback) - ref_count=%d\n", cnt );
            *pp = static_cast<IDeckLinkMemoryAllocator*>(this);
            return S_OK;
        }

        if( IsEqualGUID( riid, IID_IUnknown ) )
        {
            int cnt = InterlockedIncrement( &ref_count );
            printf( "Alloc::QueryInterface(IUnknown) - ref_count=%d\n", cnt );
            *pp = static_cast<IUnknown*>(this);
            return S_OK;
        }

        return E_NOINTERFACE;
    }

    virtual HRESULT STDMETHODCALLTYPE AllocateBuffer( unsigned long buf_size, void** pBuffer )
    {
        assert( buf_size <= g_frame_size );

        if( buf_size > g_frame_size )
        {
            printf( "Alloc::AllocateBuffer failed: buf_size=%lu is too large.\n", buf_size );
            return E_OUTOFMEMORY;
        }

        EnterCriticalSection(&buffers_lock);

        if( free_buffers.empty() )
        {
            LeaveCriticalSection(&buffers_lock);
            printf("Alloc::AllocateBuffer failed: no more buffers.\n");
            return E_OUTOFMEMORY;
        }

        char* ptr = 0;

        try
        {
            size_t n = free_buffers.size() - 1;

            reserved_buffers.insert( ptr = free_buffers[n] );
            free_buffers.resize(n);
        }
        catch(...)
        {
            LeaveCriticalSection(&buffers_lock);
            return E_OUTOFMEMORY;
        }

        LeaveCriticalSection(&buffers_lock);
        *pBuffer = ptr;
        return S_OK;
    }

    virtual HRESULT STDMETHODCALLTYPE ReleaseBuffer( void* buf )
    {
        EnterCriticalSection(&buffers_lock);

        std::set<char*>::iterator it = reserved_buffers.find( (char*)buf );

        assert( it != reserved_buffers.end() );

        if( it != reserved_buffers.end() )
        {
            free_buffers.push_back(*it);
            reserved_buffers.erase(it);
        }

        LeaveCriticalSection(&buffers_lock);
        return S_OK;
    }

    virtual HRESULT STDMETHODCALLTYPE Commit(void)
    {
        printf("Alloc::Commit\n");
        return S_OK;
    }

    virtual HRESULT STDMETHODCALLTYPE Decommit(void)
    {
        printf("Alloc::Decommit\n");
        return S_OK;
    }
};

//=====================================================================================================================
void test_iteration( IDeckLink* deckLink, unsigned long j )
{
    printf( "\nVideo+Audio Capture Testing Iteration #%lu started!\n", j );

    IDeckLinkInput* input;
    printf("deckLink->QueryInterface(IID_IDeckLinkInput)...\n");
    HRESULT hr = deckLink->QueryInterface( IID_IDeckLinkInput, (void**)&input );
    if( FAILED(hr) )
    {
        fprintf( stderr, "deckLink->QueryInterface(IID_IDeckLinkInput) failed\n" );
        return;
    }

#ifndef DISABLE_CUSTOM_ALLOCATOR
    IDeckLinkMemoryAllocator* alloc = Alloc::CreateInstance();
    printf("input->SetVideoInputFrameMemoryAllocator...\n");
    hr = input->SetVideoInputFrameMemoryAllocator(alloc);
    if( FAILED(hr) )
    {
        fprintf( stderr, "input->SetVideoInputFrameMemoryAllocator(obj) failed\n" );
    }
    else
    {
#endif
        printf("input->EnableVideoInput...\n");
        hr = input->EnableVideoInput( g_mode, bmdFormat8BitYUV, 0 );
        if( FAILED(hr) )
        {
            fprintf( stderr, "input->EnableVideoInput failed\n" );
        }
        else
        {
            printf("input->EnableAudioInput...\n");
            hr = input->EnableAudioInput( bmdAudioSampleRate48kHz, bmdAudioSampleType32bitInteger, 16 );
            if( FAILED(hr) )
            {
                fprintf( stderr, "input->EnableAudioInput failed\n" );
            }
            else
            {
#ifndef DISABLE_INPUT_CALLBACK
                InputCallback callback;
                printf("input->SetCallback(obj)...\n");
                hr = input->SetCallback(&callback);
                if( FAILED(hr) )
                {
                    fprintf( stderr, "input->SetCallback failed\n" );
                }
                else
                {
#endif
                    printf("input->StartStreams...\n");
                    hr = input->StartStreams();
                    if( FAILED(hr) )
                    {
                        fprintf( stderr, "input->StartStreams failed\n" );
                    }
                    else
                    {
                        printf("Video+Audio Capture working 5 sec...\n");
                        Sleep(5*1000);

                        printf("input->StopStreams...\n");
                        hr = input->StopStreams();
                        if( FAILED(hr) )
                        {
                            fprintf( stderr, "input->StopStreams failed\n" );
                        }
                    }

#ifndef DISABLE_INPUT_CALLBACK
                    printf("input->SetCallback(NULL)...\n");
                    hr = input->SetCallback(NULL);
                    if( FAILED(hr) )
                    {
                        fprintf( stderr, "input->SetCallback failed\n" );
                    }
                }
#endif

                printf("input->DisableAudioInput...\n");
                hr = input->DisableAudioInput();
                if( FAILED(hr) )
                {
                    fprintf( stderr, "input->DisableAudioInput failed\n" );
                }
            }

            printf("input->DisableVideoInput...\n");
            hr = input->DisableVideoInput();
            if( FAILED(hr) )
            {
                fprintf( stderr, "input->DisableVideoInput failed\n" );
            }
        }

#ifndef DISABLE_CUSTOM_ALLOCATOR
#if 1
        hr = input->SetVideoInputFrameMemoryAllocator(NULL);
        if( FAILED(hr) )
        {
            fprintf( stderr, "input->SetVideoInputFrameMemoryAllocator(NULL) failed\n" );
        }
#endif
    }

    printf("input->Release...\n");
    input->Release();

    alloc->Release();
#else
    printf("input->Release...\n");
    input->Release();
#endif

    printf( "Video+Audio Capture Stopped.\n"
            "Allocating+Reading+Deallocating 50 memory buffers of 1920x1080x4 bytes each.\n" );

    unsigned memory_sum = 0;

    for( unsigned k = 0; k < 50; ++k )
    {
        // memory allocating+reading+deallocating iteration
        unsigned* buf = new unsigned[1920*1080];

        for( size_t j = 0; j < 1920*1080; ++j )
        {
            memory_sum += buf[j];
        }

        delete[] buf;
    }

    printf( "Video+Audio Capture Testing Iteration #%lu finished (memory_sum=0x%x)!\n\n", j, memory_sum );
}

//=====================================================================================================================
int main( int argc, char* argv[] )
{
    IDeckLinkIterator*  deckLinkIterator;
    IDeckLink*  deckLink;
    int  numDevices = 0;
    HRESULT  hr;

    //  Initialize COM on this thread
    hr = CoInitialize(NULL);
    if( FAILED(hr) )
    {
        fprintf( stderr, "Initialization of COM failed - hr = %08x.\n", hr );
        return 1;
    }

    // Create an IDeckLinkIterator object to enumerate all DeckLink cards in the system
    hr = CoCreateInstance( CLSID_CDeckLinkIterator,NULL,CLSCTX_ALL, IID_IDeckLinkIterator, (void**)&deckLinkIterator );
    if( FAILED(hr) )
    {
        fprintf( stderr, "A DeckLink iterator could not be created.  The DeckLink drivers may not be installed.\n" );
        CoUninitialize();
        return 1;
    }

    {
        // We can get the version of the API like this:
        IDeckLinkAPIInformation* deckLinkAPIInformation;
        hr = deckLinkIterator->QueryInterface( IID_IDeckLinkAPIInformation, (void**)&deckLinkAPIInformation );
        if( hr == S_OK )
        {
            LONGLONG  deckLinkVersion;
            int  dlVerMajor, dlVerMinor, dlVerPoint;

            // We can also use the BMDDeckLinkAPIVersion flag with GetString
            deckLinkAPIInformation->GetInt( BMDDeckLinkAPIVersion, &deckLinkVersion );

            dlVerMajor = (deckLinkVersion & 0xFF000000) >> 24;
            dlVerMinor = (deckLinkVersion & 0x00FF0000) >> 16;
            dlVerPoint = (deckLinkVersion & 0x0000FF00) >> 8;

            printf( "DeckLink API version: %d.%d.%d\n", dlVerMajor, dlVerMinor, dlVerPoint );

            deckLinkAPIInformation->Release();
        }
    }

    hr = deckLinkIterator->Next(&deckLink); // first device
    if( FAILED(hr) )
    {
        fprintf( stderr, "No Blackmagic Design devices were found.\n" );
        deckLinkIterator->Release();
        CoUninitialize();
        return 1;
    }

    for( unsigned long j = 0; j < 0x80000000UL; ++j )
    {
        test_iteration( deckLink, j+1 );
        printf("\nWaiting 2 sec...\n");
        Sleep(2*1000);
    }

    deckLink->Release();
    deckLinkIterator->Release();
    CoUninitialize();
    return 0;
}

// allocator_test.cpp : Defines the entry point for the console application.
//

#include <comutil.h>
#include "DeckLinkAPI_h.h"

BMDDisplayMode g_mode = bmdModePAL;

class Alloc: public IDeckLinkMemoryAllocator
{
    LONG volatile ref_counter;
    LONG volatile buf_count;
public:
    Alloc(): ref_counter(1), buf_count(0) {}
    ~Alloc() { printf("Alloc::~Alloc current buf count %ld\n", buf_count); }

    virtual ULONG STDMETHODCALLTYPE AddRef()
    {
        int cnt = InterlockedIncrement( &ref_counter );
        printf("Alloc::AddRef (counter=%d)\n", cnt );
        return cnt;
    }

    virtual ULONG STDMETHODCALLTYPE Release()
    {
        int cnt = InterlockedDecrement( &ref_counter );
        printf("Alloc::Release (counter=%d)\n", cnt );
        if( cnt <= 0 )
            delete this;
        return cnt;
    }

    virtual HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void **ppvObject )
    {
        printf("Alloc::QueryInterface\n");
        return E_NOINTERFACE;
    }

    virtual HRESULT STDMETHODCALLTYPE AllocateBuffer( 
        /* [in] */ unsigned long bufferSize,
        /* [out] */ void **allocatedBuffer)
    {
        int cnt = InterlockedIncrement(&buf_count);
        //printf("Alloc::AllocateBuffer %08x (size %ld, total_count=%d)\n", (unsigned)*allocatedBuffer, bufferSize, cnt );
        while(true)
        {
            *allocatedBuffer = new char[bufferSize];
            if( (uintptr_t)(*allocatedBuffer) % 16 == 0 )
                return S_OK;
            delete[] *allocatedBuffer;
        }
        
        return S_OK;
    }

    virtual HRESULT STDMETHODCALLTYPE ReleaseBuffer( 
        /* [in] */ void *buffer)
    {
        int cnt = InterlockedDecrement(&buf_count);
        //printf("Alloc::DeallocateBuffer %08x (total_count=%d)\n", (int)buffer, cnt );
        delete[] buffer;
        return S_OK;
    }

    virtual HRESULT STDMETHODCALLTYPE Commit( void)
    {
        printf("Alloc::Commit\n");
        return S_OK;
    }

    virtual HRESULT STDMETHODCALLTYPE Decommit( void)
    {
        printf("Alloc::Decommit\n");
        return S_OK;
    }
};

void iteration( IDeckLink* deckLink )
{
    printf("Iteration started\n" );
    IDeckLinkInput* input;
    HRESULT result = deckLink->QueryInterface( IID_IDeckLinkInput, (void**)&input );
    if( FAILED(result) )
    {
        fprintf(stderr, "deckLink->QueryInterface failed\n");
        return;
    }

    Alloc* alloc = new Alloc();
    printf("input->SetVideoInputFrameMemoryAllocator...\n");
    result = input->SetVideoInputFrameMemoryAllocator( alloc );
    alloc->Release();
    if( FAILED(result) )
    {
        input->Release();
        fprintf(stderr, "input->SetVideoInputFrameMemoryAllocator failed\n");
        return;
    }

    printf("input->EnableVideoInput...\n");
    result = input->EnableVideoInput( g_mode, bmdFormat8BitYUV, 0 );
    if( FAILED(result) )
    {
        input->Release();
        fprintf(stderr, "input->EnableVideoInput failed\n");
        return;
    }

    printf("input->EnableAudioInput...\n");
    result = input->EnableAudioInput( bmdAudioSampleRate48kHz, bmdAudioSampleType32bitInteger, 16 );
    if( FAILED(result) )
    {
        input->Release();
        fprintf(stderr, "input->EnableAudioInput failed\n");
        return;
    }

    printf("input->StartStreams...\n");
    result = input->StartStreams();
    if( FAILED(result) )
    {
        input->Release();
        fprintf(stderr, "input->StartStreams failed\n");
        return;
    }

    printf( "Started. working 5 sec\n");
    Sleep(5 * 1000);
    printf("input->StopStreams...\n");
    result = input->StopStreams();
    if( FAILED(result) )
    {
        input->Release();
        fprintf(stderr, "input->StopStreams failed\n");
        return;
    }

    printf("input->DisableAudioInput...\n");
    input->DisableAudioInput();
    printf("input->DisableVideoInput...\n");
    input->DisableVideoInput();
    printf("input->Release...\n");
    input->Release();
    printf("Stopped. some memory operations for tests\n");

    int global_tests = 0;
    for( int i = 0; i < 50; ++i )
    {
        int* buf = new int[ 1920 * 1080 ];
        int sum = 0;
        for( int i = 0; i < 1920 * 1080; ++i )
            sum += buf[i];
        global_tests += sum;
        delete[] buf;
    }
    printf("Iteration finished successful (sum=%d)\n", global_tests );

    printf("\n");
}

int main( int argc, char* argv[] )
{
    IDeckLinkIterator*  deckLinkIterator;
    IDeckLink*  deckLink;
    int  numDevices = 0;
    HRESULT  result;

    //  Initialize COM on this thread
    result = CoInitialize(NULL);
    if (FAILED(result))
    {
        fprintf(stderr, "Initialization of COM failed - result = %08x.\n", result);
        return 1;
    }

    // Create an IDeckLinkIterator object to enumerate all DeckLink cards in the system
    result = CoCreateInstance(CLSID_CDeckLinkIterator, NULL, CLSCTX_ALL, IID_IDeckLinkIterator, (void**)&deckLinkIterator);
    if (FAILED(result))
    {
        fprintf(stderr, "A DeckLink iterator could not be created.  The DeckLink drivers may not be installed.\n");
        return 1;
    }

    {
        // We can get the version of the API like this:
        IDeckLinkAPIInformation*	deckLinkAPIInformation;
        result = deckLinkIterator->QueryInterface(IID_IDeckLinkAPIInformation, (void**)&deckLinkAPIInformation);
        if (result == S_OK)
        {
            LONGLONG  deckLinkVersion;
            int  dlVerMajor, dlVerMinor, dlVerPoint;

            // We can also use the BMDDeckLinkAPIVersion flag with GetString
            deckLinkAPIInformation->GetInt(BMDDeckLinkAPIVersion, &deckLinkVersion);

            dlVerMajor = (deckLinkVersion & 0xFF000000) >> 24;
            dlVerMinor = (deckLinkVersion & 0x00FF0000) >> 16;
            dlVerPoint = (deckLinkVersion & 0x0000FF00) >> 8;

            printf("DeckLinkAPI version: %d.%d.%d\n", dlVerMajor, dlVerMinor, dlVerPoint);

            deckLinkAPIInformation->Release();
        }
    }

    result = deckLinkIterator->Next(&deckLink); // first device
    if( FAILED(result) )
    {
        fprintf(stderr, "No Blackmagic Design devices were found.\n");
        deckLinkIterator->Release();
        return 1;
    }

    for( int i = 0; i < 50; ++i )
        iteration(deckLink);

    deckLink->Release();
    deckLinkIterator->Release();

    // Uninitalize COM on this thread
    CoUninitialize();

    return 0;
}


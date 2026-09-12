#pragma once
class Backend;
class QQuickWindow;
void runUiTests(Backend *, QQuickWindow *);
void runUiAudit(Backend*,QQuickWindow*);

void runRecoveryTests(Backend*,QQuickWindow*);

void runLyricsTests(Backend*,QQuickWindow*);

void runFeatureTests(Backend*,QQuickWindow*);
void runSearchSelectionTests(Backend*,QQuickWindow*);

void runVisualPolishTests(Backend*,QQuickWindow*);

void runServerTests(Backend*,QQuickWindow*);

void runRemoteServerTest(Backend*,QQuickWindow*);

void runQolTests(Backend*,QQuickWindow*);

void runLibraryQolTests(Backend*,QQuickWindow*);

void runVisualDelightTests(Backend*,QQuickWindow*);

void runAudioIndicatorTests(Backend*,QQuickWindow*);

void runInteractionTests(Backend *b, QQuickWindow *w);
void runFolderImportTests(Backend *b, QQuickWindow *w);

void runLocalArtworkTests(Backend *, QQuickWindow *);

void runOnlineArtworkTests(Backend *, QQuickWindow *);

void runOnlineArtworkLiveTests(Backend *, QQuickWindow *);
void runProductPolishTests(Backend *, QQuickWindow *);

void runLibraryPolishTests(Backend *, QQuickWindow *);

void runPlaybackPolishTests(Backend *, QQuickWindow *);

void runVisualRefinementTests(Backend *, QQuickWindow *);

void runListeningRefinementTests(Backend *backend,QQuickWindow *window);

void runInteractionRefinementTests(Backend*,QQuickWindow*);

void runImmersivePolishTests(Backend*,QQuickWindow*);

void runImmersiveEdgeTests(Backend*,QQuickWindow*);
void runImmersivePreferencesTest(Backend*,QQuickWindow*);

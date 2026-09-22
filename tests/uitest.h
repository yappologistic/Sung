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

void runAmbientImmersiveTests(Backend*,QQuickWindow*);
void runPersonalizationTests(Backend*,QQuickWindow*);
void runHomeRailTests(Backend*,QQuickWindow*);
void runOnboardingTests(Backend*,QQuickWindow*);
void runLibraryExchangeTests(Backend*,QQuickWindow*);
void runBackdropPulseTests(Backend*,QQuickWindow*);
void runPlaybackMemoryTests(Backend*,QQuickWindow*);
void runFootprintTests(Backend*,QQuickWindow*);
void runInterfaceAuditTests(Backend*,QQuickWindow*);

void runTourCapture(Backend*,QQuickWindow*);
void runDynamicColorTests(Backend*,QQuickWindow*);
void runNavigationMotionTests(Backend*,QQuickWindow*);
void runArtistHeroTests(Backend*,QQuickWindow*);
void runSingAlongTests(Backend*,QQuickWindow*);
void runCrossfadeUiTests(Backend*,QQuickWindow*);
void runTrackDetailsTests(Backend*,QQuickWindow*);
void runQueueHistoryTests(Backend*,QQuickWindow*);
void runListeningStatsTests(Backend*,QQuickWindow*);
void runPlaylistVersionsTests(Backend*,QQuickWindow*);
void runWindowWashTests(Backend*,QQuickWindow*);
void runMaterialFoundationTests(Backend*,QQuickWindow*);
void runMaterialComponentTests(Backend*,QQuickWindow*);
void runMaterialDetailTests(Backend*,QQuickWindow*);
void runMaterialExpressiveTests(Backend*,QQuickWindow*);
void runMaterialSizingTests(Backend*,QQuickWindow*);
void runMaterialSchemeTests(Backend*,QQuickWindow*);
void runMaterialGrainTests(Backend*,QQuickWindow*);
void runMaterialScaleTests(Backend*,QQuickWindow*);
void runMaterialControlsTests(Backend*,QQuickWindow*);
void runMaterialAnatomyTests(Backend*,QQuickWindow*);
void runMaterialEmphasisTests(Backend*,QQuickWindow*);

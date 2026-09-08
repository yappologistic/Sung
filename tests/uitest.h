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

#ifndef EL6EX_H
#define EL6EX_H

#include <QObject>
#include "../p6el.h"

class QtEL6 : public QObject, public EL6
{
    Q_OBJECT
public:
    QtEL6();
    void ShowPopupImpl(int x, int y);
    bool GetPauseEnable();
	void SetPauseEnable(bool en);

public slots:
    void updateFPS();
};

#endif // EL6EX_H

#include <QtGui>

#include "../event.h"
#include "../osd.h"
#include "../error.h"
#include "../pc6001v.h"
#include "../typedef.h"
#include "../console.h"
#include "../error.h"
#include "../osd.h"

#include "qtp6vxapplication.h"

///////////////////////////////////////////////////////////
// フォントファイルチェック(無ければ作成する)
///////////////////////////////////////////////////////////
bool CheckFont( void )
{
    char FontFile[PATH_MAX];
    bool ret = true;

    sprintf( FontFile, "%s%s/%s", OSD_GetConfigPath(), FONT_DIR, FONTH_FILE );
    ret |= ( !OSD_FileExist( FontFile ) && !OSD_CreateFont( FontFile, NULL, FSIZE ) );

    sprintf( FontFile, "%s%s/%s", OSD_GetConfigPath(), FONT_DIR, FONTZ_FILE );
    ret |= ( !OSD_FileExist( FontFile ) && !OSD_CreateFont( NULL, FontFile, FSIZE ) );

    return ret;
}

///////////////////////////////////////////////////////////
// ROMファイル存在チェック&機種変更
///////////////////////////////////////////////////////////
bool SerchRom( CFG6 *cfg )
{
    char RomSerch[PATH_MAX];

    int IniModel = cfg->GetModel();
    sprintf( RomSerch, "%s*.%2d", cfg->GetRomPath(), IniModel );
    if( OSD_FileExist( RomSerch ) ){
        Error::SetError( Error::NoError );
        return true;
    }

    int models[] = { 60, 62, 66 };
    for( int i=0; i < COUNTOF(models); i++ ){
        sprintf( RomSerch, "%s*.%2d", cfg->GetRomPath(), models[i] );
        if( OSD_FileExist( RomSerch ) ){
            cfg->SetModel( models[i] );
            cfg->Write();
            Error::SetError( Error::RomChange );
            return true;
        }
    }
    Error::SetError( Error::NoRom );
    return false;
}

QtP6VXApplication::QtP6VXApplication(int &argc, char **argv) :
    QtSingleApplication(argc, argv)
  , P6Core(NULL)
  , Restart(EL6::Quit)
  , Adaptor(new EmulationAdaptor())
{
    qRegisterMetaType<HWINDOW>("HWINDOW");

    QThread* emulationThread = new QThread(this);
    emulationThread->start();
    Adaptor->moveToThread(emulationThread);

    connect(this, SIGNAL(initialized()), this, SLOT(executeEmulation()));
    connect(this, SIGNAL(vmPrepared()), Adaptor, SLOT(doEventLoop()));
    connect(this, SIGNAL(lastWindowClosed()), this, SLOT(terminateEmulation()));
    connect(Adaptor, SIGNAL(finished()), this, SLOT(postExecuteEmulation()));
}

QtP6VXApplication::~QtP6VXApplication()
{
    Adaptor->deleteLater();
}

void QtP6VXApplication::startup()
{
    // 二重起動禁止
    if( isRunning() ) exit();

    // 設定ファイルパスを作成
    if(!OSD_CreateConfigPath()) exit();

    // OSD関連初期化
    if( !OSD_Init() ){
        Error::SetError( Error::InitFailed );
        OSD_Message( (char *)Error::GetErrorText(), MSERR_ERROR, OSDR_OK | OSDM_ICONERROR );
        OSD_Quit();	// 終了処理
        exit();
    }

    // フォントファイルチェック
    if( !CheckFont() ){
        Error::SetError( Error::FontCreateFailed );
        OSD_Message( (char *)Error::GetErrorText(), MSERR_ERROR, OSDR_OK | OSDM_ICONWARNING );
        Error::SetError( Error::NoError );
    }

    // コンソール用フォント読込み
    char FontZ[PATH_MAX], FontH[PATH_MAX];
    sprintf( FontZ, "%s%s/%s", OSD_GetConfigPath(), FONT_DIR, FONTZ_FILE );
    sprintf( FontH, "%s%s/%s", OSD_GetConfigPath(), FONT_DIR, FONTH_FILE );
    if( !JFont::OpenFont( FontZ, FontH ) ){
        Error::SetError( Error::FontLoadFailed );
        OSD_Message( (char *)Error::GetErrorText(), MSERR_ERROR, OSDR_OK | OSDM_ICONERROR );
        Error::SetError( Error::NoError );
    }


    // INIファイル読込み
    if( !Cfg.Init() ){
        switch( Error::GetError() ){
        case Error::IniDefault:
            OSD_Message( (char *)Error::GetErrorText(), MSERR_ERROR, OSDR_OK | OSDM_ICONWARNING );
            Error::SetError( Error::NoError );
            break;

        default:
            OSD_Message( (char *)Error::GetErrorText(), MSERR_ERROR, OSDR_OK | OSDM_ICONERROR );
            OSD_Quit();			// 終了処理
            exit();
        }
    }

    emit initialized();
}

void QtP6VXApplication::layoutBitmap(HWINDOW Wh, int x, int y, double aspect, QImage image)
{
    //QtではSceneRectの幅を返す
    QGraphicsView* view = static_cast<QGraphicsView*>(Wh);
    Q_ASSERT(view);
    QGraphicsScene* scene = view->scene();

    QGraphicsPixmapItem* item = dynamic_cast<QGraphicsPixmapItem*>(scene->itemAt(x, y));
    if(item == NULL){
        item = new QGraphicsPixmapItem(QPixmap::fromImage(image), NULL, scene);
    } else {
        item->setPixmap(QPixmap::fromImage(image));
    }
    item->setPos(x, y);
}

//仮想マシンを開始させる
void QtP6VXApplication::executeEmulation()
{
    // ROMファイル存在チェック&機種変更
    if( SerchRom( &Cfg ) ){
        if( Error::GetError() != Error::NoError ){
            OSD_Message( (char *)Error::GetErrorText(), MSERR_ERROR, OSDM_OK | OSDM_ICONWARNING );
            Error::SetError( Error::NoError );
        }
    }else{
        if(OSD_Message( QString("ROMファイルが見つかりません。\n"
                                "ROMフォルダ(%1)にROMファイルをコピーするか、"
                                "別のROMフォルダを指定してください。\n"
                                "別のROMフォルダを指定しますか?").arg(Cfg.GetRomPath()).toUtf8().data(), MSERR_ERROR, OSDM_YESNO | OSDM_ICONERROR ) == OSDR_YES){
            //ROMフォルダ再設定
            char folder[PATH_MAX];
            strncpy(folder, Cfg.GetRomPath(), PATH_MAX);
            Delimiter(folder);
            OSD_FolderDiaog(NULL, folder);
            UnDelimiter(folder);

            if(strlen(folder) > 0){
                Cfg.SetRomPath(folder);
                Cfg.Write();
                Restart = EL6::Restart;
            } else {
                exit();
            }
        } else {
            exit();
        }
        emit vmRestart();
        return;
    }

    // 機種別P6オブジェクト確保
    P6Core = new EL6;
    if( !P6Core ){
        exit();
    }

    // VM初期化
    if( !P6Core->Init( &Cfg ) ){
        if(Error::GetError() == Error::RomCrcNG){
            // CRCが合わない場合
            int ret = OSD_Message( "ROMイメージのCRCが不正です。\n"
                                   "CRCが一致しないROMを使用すると、予期せぬ不具合を引き起こす可能性があります。\n"
                                   "それでも起動しますか?",
                                   MSERR_ERROR, OSDM_YESNO | OSDM_ICONWARNING );
            if(ret == OSDR_YES) {
                Cfg.SetCheckCRC(false);
                Cfg.Write();
                Restart = EL6::Restart;
            }
        } else {
            OSD_Message( (char *)Error::GetErrorText(), MSERR_ERROR, OSDR_OK | OSDM_ICONERROR );
            exit();
        }
    }

    switch( Restart ){
    case EL6::Dokoload:	// どこでもLOAD?
        P6Core->DokoDemoLoad( Cfg.GetDokoFile() );
        break;

    case EL6::Replay:	// リプレイ再生?
    {
        char temp[PATH_MAX];
        strncpy( temp, Cfg.GetDokoFile(), PATH_MAX );
        P6Core->DokoDemoLoad( temp );
        P6Core->REPLAY::StartReplay( temp );
    }
        break;

    default:
        break;
    }

    // VM実行
    Adaptor->setEmulationObj(P6Core);
    emit vmPrepared();
    P6Core->Start();
}

//仮想マシン終了後の処理
void QtP6VXApplication::postExecuteEmulation()
{
    Restart = Adaptor->getReturnCode();
    Adaptor->setEmulationObj(NULL);
    P6Core->Stop();
    delete P6Core;	// P6オブジェクトを開放
    P6Core = NULL;

    // 再起動ならばINIファイル再読込み
    if( Restart == EL6::Restart ){
        if( !Cfg.Init() ){
            Error::SetError( Error::IniDefault );
            OSD_Message( (char *)Error::GetErrorText(), MSERR_ERROR, OSDR_OK | OSDM_ICONWARNING );
            Error::SetError( Error::NoError );
        }
    }

    if( Restart == EL6::Quit ){
        // 終了処理
        OSD_Quit();
        exit();
    } else {
        emit vmRestart();
    }
}

void QtP6VXApplication::terminateEmulation()
{
    OSD_PushEvent( EV_QUIT );
}

bool QtP6VXApplication::notify ( QObject * receiver, QEvent * event )
{
    Event ev;
    ev.type = EV_NOEVENT;

    switch(event->type()){
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    {
        QKeyEvent* ke = dynamic_cast<QKeyEvent*>(event);
        Q_ASSERT(ke);
        ev.type        = event->type() == QEvent::KeyPress ? EV_KEYDOWN : EV_KEYUP;
        ev.key.state   = event->type() == QEvent::KeyPress ? true : false;
        ev.key.sym     = OSD_ConvertKeyCode( ke->key() );
        ev.key.mod	   = (PCKEYmod)(
                    ( ke->modifiers() & Qt::ShiftModifier ? KVM_SHIFT : KVM_NONE )
                    | ( ke->modifiers() & Qt::ControlModifier ? KVM_CTRL : KVM_NONE )
                    | ( ke->modifiers() & Qt::AltModifier ? KVM_SHIFT : KVM_NONE )
                    | ( ke->modifiers() & Qt::MetaModifier ? KVM_META : KVM_NONE )
                    | ( ke->modifiers() & Qt::KeypadModifier ? KVM_NUM : KVM_NONE )
                    //#PENDING CAPSLOCKは検出できない？
                    //| ( ke->modifiers() & Qt::caps ? KVM_NUM : KVM_NONE )
                    );
        ev.key.unicode = ke->text().utf16()[0];
        OSD_PushEvent(ev);
        break;
    }
    case QEvent::ContextMenu:
    {
        ev.type        = EV_CONTEXTMENU;
        OSD_PushEvent(ev.type);
        break;
    }
    default:;
    }

    if(ev.type == EV_NOEVENT){
        return QtSingleApplication::notify(receiver, event);
    } else {
        return true;
    }
}
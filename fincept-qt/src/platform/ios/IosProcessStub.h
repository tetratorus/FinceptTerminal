#pragma once
// iOS has no subprocesses (Qt builds QtCore there with QT_FEATURE_process off,
// so <QProcess> only declares QProcessEnvironment). This stand-in keeps the
// desktop call sites compiling: every start fails with FailedToStart, the
// same path the app already handles when python/uv/an MCP binary is missing.
#include <QtCore/qprocess.h>

#if !QT_CONFIG(process)
#    include <QtCore/QIODevice>
#    include <QtCore/QStringList>
#    include <QtCore/QTimer>

#    include <functional>

class QProcess : public QIODevice {
    Q_OBJECT
  public:
    enum ProcessError { FailedToStart, Crashed, Timedout, ReadError, WriteError, UnknownError };
    Q_ENUM(ProcessError)
    enum ProcessState { NotRunning, Starting, Running };
    Q_ENUM(ProcessState)
    enum ProcessChannel { StandardOutput, StandardError };
    Q_ENUM(ProcessChannel)
    enum ProcessChannelMode {
        SeparateChannels,
        MergedChannels,
        ForwardedChannels,
        ForwardedOutputChannel,
        ForwardedErrorChannel
    };
    Q_ENUM(ProcessChannelMode)
    enum InputChannelMode { ManagedInputChannel, ForwardedInputChannel };
    Q_ENUM(InputChannelMode)
    enum ExitStatus { NormalExit, CrashExit };
    Q_ENUM(ExitStatus)

    enum class UnixProcessFlag : quint32 {
        ResetSignalHandlers = 0x0001,
        IgnoreSigPipe = 0x0002,
        CloseFileDescriptors = 0x0010,
        UseVFork = 0x0020,
        CreateNewSession = 0x0040,
        DisconnectControllingTerminal = 0x0080,
        ResetIds = 0x0100,
    };
    Q_DECLARE_FLAGS(UnixProcessFlags, UnixProcessFlag)
    struct UnixProcessParameters {
        UnixProcessFlags flags = {};
        int lowestFileDescriptorToClose = 0;
    };

    explicit QProcess(QObject* parent = nullptr) : QIODevice(parent) {}

    void start(const QString& program, const QStringList& arguments = {}, OpenMode = ReadWrite) {
        program_ = program;
        arguments_ = arguments;
        fail_to_start();
    }
    void start(OpenMode = ReadWrite) { fail_to_start(); }
    void startCommand(const QString& command, OpenMode = ReadWrite) {
        program_ = command;
        fail_to_start();
    }
    bool startDetached(qint64* = nullptr) { return false; }

    QString program() const { return program_; }
    void setProgram(const QString& program) { program_ = program; }
    QStringList arguments() const { return arguments_; }
    void setArguments(const QStringList& arguments) { arguments_ = arguments; }

    ProcessChannelMode processChannelMode() const { return channel_mode_; }
    void setProcessChannelMode(ProcessChannelMode mode) { channel_mode_ = mode; }
    InputChannelMode inputChannelMode() const { return ManagedInputChannel; }
    void setInputChannelMode(InputChannelMode) {}
    ProcessChannel readChannel() const { return StandardOutput; }
    void setReadChannel(ProcessChannel) {}
    void closeReadChannel(ProcessChannel) {}
    void closeWriteChannel() {}
    void setStandardInputFile(const QString&) {}
    void setStandardOutputFile(const QString&, OpenMode = Truncate) {}
    void setStandardErrorFile(const QString&, OpenMode = Truncate) {}
    void setStandardOutputProcess(QProcess*) {}

    void setChildProcessModifier(const std::function<void(void)>&) {}
    void setUnixProcessParameters(const UnixProcessParameters&) {}
    void setUnixProcessParameters(UnixProcessFlags) {}

    QString workingDirectory() const { return working_dir_; }
    void setWorkingDirectory(const QString& dir) { working_dir_ = dir; }
    void setEnvironment(const QStringList& environment) { env_ = environment; }
    QStringList environment() const { return env_; }
    void setProcessEnvironment(const QProcessEnvironment& environment) { env_ = environment.toStringList(); }
    QProcessEnvironment processEnvironment() const {
        QProcessEnvironment e;
        for (const QString& kv : env_) {
            const qsizetype eq = kv.indexOf(u'=');
            if (eq > 0)
                e.insert(kv.left(eq), kv.mid(eq + 1));
        }
        return e;
    }

    ProcessError error() const { return error_; }
    ProcessState state() const { return NotRunning; }
    qint64 processId() const { return 0; }

    bool waitForStarted(int = 30000) { return false; }
    bool waitForReadyRead(int = 30000) override { return false; }
    bool waitForBytesWritten(int = 30000) override { return false; }
    bool waitForFinished(int = 30000) { return false; }

    QByteArray readAllStandardOutput() { return {}; }
    QByteArray readAllStandardError() { return {}; }
    int exitCode() const { return -1; }
    ExitStatus exitStatus() const { return NormalExit; }

    bool isSequential() const override { return true; }

    static int execute(const QString&, const QStringList& = {}) { return -2; }
    static bool startDetached(const QString&, const QStringList& = {}, const QString& = QString(), qint64* = nullptr) {
        return false;
    }
    static QStringList systemEnvironment() { return QProcessEnvironment::systemEnvironment().toStringList(); }
    static QString nullDevice() { return QStringLiteral("/dev/null"); }

    // Same quoting rules as QProcess::splitCommand (double quotes, "" = literal quote).
    static QStringList splitCommand(QStringView command) {
        QStringList args;
        QString tmp;
        int quote_count = 0;
        bool in_quote = false;
        for (qsizetype i = 0; i < command.size(); ++i) {
            if (command.at(i) == u'"') {
                ++quote_count;
                if (quote_count == 3) {
                    quote_count = 0;
                    tmp += command.at(i);
                }
                continue;
            }
            if (quote_count) {
                if (quote_count == 1)
                    in_quote = !in_quote;
                quote_count = 0;
            }
            if (!in_quote && command.at(i).isSpace()) {
                if (!tmp.isEmpty()) {
                    args += tmp;
                    tmp.clear();
                }
            } else {
                tmp += command.at(i);
            }
        }
        if (!tmp.isEmpty())
            args += tmp;
        return args;
    }

  public Q_SLOTS:
    void terminate() {}
    void kill() {}

  Q_SIGNALS:
    void started();
    void finished(int exitCode, QProcess::ExitStatus exitStatus = NormalExit);
    void errorOccurred(QProcess::ProcessError error);
    void stateChanged(QProcess::ProcessState state);
    void readyReadStandardOutput();
    void readyReadStandardError();

  protected:
    qint64 readData(char*, qint64) override { return -1; }
    qint64 writeData(const char*, qint64) override { return -1; }

  private:
    void fail_to_start() {
        error_ = FailedToStart;
        setErrorString(QStringLiteral("Subprocesses are not available on iOS"));
        // Queued like the real QProcess, so callers that connect after start() still see it.
        QTimer::singleShot(0, this, [this] { emit errorOccurred(FailedToStart); });
    }

    QString program_;
    QStringList arguments_;
    QString working_dir_;
    QStringList env_;
    ProcessChannelMode channel_mode_ = SeparateChannels;
    ProcessError error_ = UnknownError;
};
Q_DECLARE_OPERATORS_FOR_FLAGS(QProcess::UnixProcessFlags)
#endif

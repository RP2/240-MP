#include <QTest>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QRegularExpression>

// Standalone test: validates modules/jellyfin/manifest.json against the
// JellyfinBackend header.  Does NOT link the backend — reads the .h as text.

static QString repoRoot() {
    // tests/jellyfin/../../  →  repo root
    return QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("../../"));
}

class ManifestTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void test_manifest_exists();
    void test_manifest_wellFormed();
    void test_required_topLevel_keys();
    void test_settings_isArray();
    void test_each_setting_has_required_fields();
    void test_setting_types_are_valid();
    void test_options_slots_exist();
    void test_libraries_setting_has_dynamic_options();
    void test_video_quality_setting_has_dynamic_options();
    void test_logout_setting_is_action();
    void test_requires_auth_consistency();
    void test_capability_filtering_settings();

private:
    QJsonObject m_manifest;
    QJsonArray  m_settings;
    QString     m_manifestPath;
    QString     m_headerPath;
    QSet<QString> m_invokables;   // parsed Q_INVOKABLE method names from the .h
};

// ── helpers ────────────────────────────────────────────────────────────────

static QJsonObject findSettingByKey(const QJsonArray &arr, const QString &key) {
    for (const auto &v : arr) {
        QJsonObject s = v.toObject();
        if (s.value("key").toString() == key)
            return s;
    }
    return {};
}

// ── init ───────────────────────────────────────────────────────────────────

void ManifestTest::initTestCase() {
    const QString root = repoRoot();
    m_manifestPath = root + QStringLiteral("/modules/jellyfin/manifest.json");
    m_headerPath   = root + QStringLiteral("/src/modules/jellyfin/JellyfinBackend.h");

    // Parse manifest
    QFile f(m_manifestPath);
    QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(f.errorString()));
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    QVERIFY2(err.error == QJsonParseError::NoError, qPrintable(err.errorString()));
    QVERIFY(!doc.isNull());
    m_manifest = doc.object();
    m_settings = m_manifest.value("settings").toArray();

    // Parse Q_INVOKABLE methods from header
    QFile h(m_headerPath);
    QVERIFY2(h.open(QIODevice::ReadOnly), qPrintable(h.errorString()));
    QTextStream ts(&h);
    QRegularExpression re(QStringLiteral("^\\s*Q_INVOKABLE\\s+\\S+\\s+(\\w+)\\s*\\("),
                          QRegularExpression::MultilineOption);
    while (!ts.atEnd()) {
        const QString line = ts.readLine();
        QRegularExpressionMatch m = re.match(line);
        if (m.hasMatch())
            m_invokables.insert(m.captured(1));
    }
    h.close();
}

// ── tests ──────────────────────────────────────────────────────────────────

void ManifestTest::test_manifest_exists() {
    QVERIFY(QFile::exists(m_manifestPath));
}

void ManifestTest::test_manifest_wellFormed() {
    QVERIFY(!m_manifest.isEmpty());
    QVERIFY(m_manifest.contains("id"));
    QVERIFY(m_manifest.contains("settings"));
}

void ManifestTest::test_required_topLevel_keys() {
    // id
    QVERIFY(m_manifest.contains("id"));
    QCOMPARE(m_manifest.value("id").toString(), QStringLiteral("com.240mp.jellyfin"));

    // name
    QVERIFY(m_manifest.contains("name"));
    QCOMPARE(m_manifest.value("name").toString(), QStringLiteral("Jellyfin"));

    // icon (string, non-empty)
    QVERIFY(m_manifest.contains("icon"));
    QVERIFY(!m_manifest.value("icon").toString().isEmpty());

    // entry_point_qml
    QVERIFY(m_manifest.contains("entry_point_qml"));
    QCOMPARE(m_manifest.value("entry_point_qml").toString(), QStringLiteral("views/Root.qml"));

    // settings
    QVERIFY(m_manifest.contains("settings"));
}

void ManifestTest::test_settings_isArray() {
    QVERIFY(m_manifest.value("settings").isArray());
    QVERIFY(!m_settings.isEmpty());
}

void ManifestTest::test_each_setting_has_required_fields() {
    for (int i = 0; i < m_settings.size(); ++i) {
        QJsonObject s = m_settings[i].toObject();
        const QString key = s.value("key").toString();
        const QString ctx = QStringLiteral("setting[%1] key=%2").arg(i).arg(key);

        // key — string, non-empty
        QVERIFY2(s.contains("key"), qPrintable(ctx));
        QVERIFY2(!s.value("key").toString().isEmpty(), qPrintable(ctx));

        // label — string
        QVERIFY2(s.contains("label"), qPrintable(ctx));
        QVERIFY2(s.value("label").isString(), qPrintable(ctx));

        // type — string
        QVERIFY2(s.contains("type"), qPrintable(ctx));
        QVERIFY2(s.value("type").isString(), qPrintable(ctx));

        const QString type = s.value("type").toString();

        // toggle must have "default"
        if (type == "toggle") {
            QVERIFY2(s.contains("default"), qPrintable(ctx + " (toggle missing default)"));
        }

        // action must have "action_slot"
        if (type == "action") {
            QVERIFY2(s.contains("action_slot"), qPrintable(ctx + " (action missing action_slot)"));
        }
    }
}

void ManifestTest::test_setting_types_are_valid() {
    const QStringList valid = {
        "toggle", "list_single", "multiselect_submenu", "action"
    };
    for (int i = 0; i < m_settings.size(); ++i) {
        QJsonObject s = m_settings[i].toObject();
        const QString type = s.value("type").toString();
        QVERIFY2(valid.contains(type),
                 qPrintable(QStringLiteral("setting[%1] has invalid type: %2").arg(i).arg(type)));
    }
}

void ManifestTest::test_options_slots_exist() {
    for (int i = 0; i < m_settings.size(); ++i) {
        QJsonObject s = m_settings[i].toObject();
        const QString key = s.value("key").toString();

        // options_slot (for dynamic options) — must be Q_INVOKABLE
        if (s.contains("options_slot")) {
            const QString slot = s.value("options_slot").toString();
            QVERIFY2(m_invokables.contains(slot),
                     qPrintable(QStringLiteral("setting '%1' options_slot '%2' not found as Q_INVOKABLE in JellyfinBackend.h")
                                .arg(key, slot)));
        }

        // action_slot — for actions, verify the slot exists via QMetaObject.
        // The spec says action_slot values don't need Q_INVOKABLE, but we
        // still check the name exists in the header (either as Q_INVOKABLE
        // or as a regular public method) so typos are caught.
        if (s.contains("action_slot")) {
            const QString slot = s.value("action_slot").toString();
            QFile h(m_headerPath);
            QVERIFY(h.open(QIODevice::ReadOnly | QIODevice::Text));
            const QString src = h.readAll();
            // Search for the method name as a word-boundary match
            QRegularExpression re(QStringLiteral("\\b%1\\b").arg(slot));
            QVERIFY2(re.match(src).hasMatch(),
                     qPrintable(QStringLiteral("setting '%1' action_slot '%2' not found in JellyfinBackend.h")
                                .arg(key, slot)));
        }
    }
}

void ManifestTest::test_libraries_setting_has_dynamic_options() {
    QJsonObject s = findSettingByKey(m_settings, "libraries");
    QVERIFY(!s.isEmpty());
    QCOMPARE(s.value("options_source").toString(), QStringLiteral("dynamic"));
    QCOMPARE(s.value("options_slot").toString(),    QStringLiteral("getLibraries"));
}

void ManifestTest::test_video_quality_setting_has_dynamic_options() {
    QJsonObject s = findSettingByKey(m_settings, "video_quality");
    QVERIFY(!s.isEmpty());
    QCOMPARE(s.value("options_source").toString(), QStringLiteral("dynamic"));
    QCOMPARE(s.value("options_slot").toString(),    QStringLiteral("getVideoQualities"));
}

void ManifestTest::test_logout_setting_is_action() {
    QJsonObject s = findSettingByKey(m_settings, "logout");
    QVERIFY(!s.isEmpty());
    QCOMPARE(s.value("type").toString(),       QStringLiteral("action"));
    QCOMPARE(s.value("action_slot").toString(), QStringLiteral("logout"));
}

void ManifestTest::test_requires_auth_consistency() {
    for (int i = 0; i < m_settings.size(); ++i) {
        QJsonObject s = m_settings[i].toObject();
        const QString key = s.value("key").toString();
        const QString ctx = QStringLiteral("setting[%1] key=%2").arg(i).arg(key);

        if (key == "enabled") {
            // "enabled" must NOT have requires_auth
            QVERIFY2(!s.contains("requires_auth"), qPrintable(ctx + " should not have requires_auth"));
        } else {
            // Every other setting must have requires_auth == true
            QVERIFY2(s.contains("requires_auth"), qPrintable(ctx + " missing requires_auth"));
            QVERIFY2(s.value("requires_auth").toBool(),
                     qPrintable(ctx + " requires_auth should be true"));
        }
    }
}

void ManifestTest::test_capability_filtering_settings() {
    const QStringList capKeys = {"intro_skip", "outro_skip"};
    for (const QString &key : capKeys) {
        QJsonObject s = findSettingByKey(m_settings, key);
        QVERIFY2(!s.isEmpty(),
                 qPrintable(QStringLiteral("setting '%1' not found in manifest").arg(key)));
        QCOMPARE(s.value("requires_capability").toString(),
                 QStringLiteral("mediasegments"));
    }
}

// ── main ───────────────────────────────────────────────────────────────────

QTEST_MAIN(ManifestTest)
#include "ManifestTest.moc"

#include "ExpressionLineEdit.h"
#include <QWheelEvent>
#include <QRegularExpression>

QString simpleDoubleString(double value)
{
     static QRegularExpression sRegex("\\.?0+$");
     auto string = QString::number(value, 'f', 6);
     string.remove(sRegex);
     return string;
}

ExpressionLineEdit::ExpressionLineEdit(QWidget *parent) : QLineEdit(parent)
{
}

void ExpressionLineEdit::wheelEvent(QWheelEvent *event)
{
    mWheelDeltaRemainder += event->angleDelta().y();
    const auto steps = mWheelDeltaRemainder / 120;
    mWheelDeltaRemainder -= steps * 120;
    if (mDecimal) {
        stepBy(event->modifiers() & Qt::ShiftModifier ? steps / 10.0 :
               event->modifiers() & Qt::ControlModifier ? steps * 10.0 : steps);
    }
    else {
        stepBy(event->modifiers() & Qt::ControlModifier ? steps * 10 : steps);
    }
    event->accept();
}

void ExpressionLineEdit::stepBy(int steps)
{
    auto ok = false;
    auto value = text().toInt(&ok);
    if (ok) {
        value = qMax(value + steps, 0);
        setText(QString::number(value));
        selectAll();
    }
}

void ExpressionLineEdit::stepBy(double steps)
{
    const auto singleStep = 0.1;
    auto ok = false;
    auto value = text().toDouble(&ok);
    if (ok) {
        value += steps * singleStep;
        setText(QString::number(value));
        selectAll();
    }
}

void ExpressionLineEdit::setText(const QString &string)
{
    auto ok = false;
    if (auto value = string.toDouble(&ok); ok) {
        auto simplified = simpleDoubleString(value);
        if (simplified != text()) {
            QLineEdit::setText(simplified);
            Q_EMIT textChanged(simplified);
        }
    }
    else {
        if (string != QLineEdit::text()) {
            QLineEdit::setText(string);
            Q_EMIT textChanged(string);
        }
    }
}

QString ExpressionLineEdit::text() const
{
    auto ok = false;
    auto string = QLineEdit::text();
    if (auto value = string.toDouble(&ok))
        return simpleDoubleString(value);
    return string;
}

bool ExpressionLineEdit::hasValue(double value) const
{
    return (text() == simpleDoubleString(value));
}

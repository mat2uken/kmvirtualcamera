# 段階6: hostからsinkを通してsourceへ送る

hostが作った既知の映像をCamera Extensionのsinkへ投入し、source利用アプリで確認する。ブラウザ・RTC・VideoToolbox接続前に、投入と消費を単独で調べる。対象は `macos/camera-extension` と新規 `macos/receiver/cmio_sink_publisher.mm`。

## 開始条件と設計入力

段階5で署名済みsourceの生成映像がcaptureできること。hostとExtensionの識別子、App Group、実SDKで取得できるclient識別情報を記録する。`KMFrameRelay.setAuthorizedProducer:` は認証済みclientの設定用であり、認証処理そのものではない。producerの確認方法をSDKと署名済み実機で成立させるまで、全clientを許可しない。

## 実装手順

1. Deviceにsink streamを追加し、sourceと別のstable ID・direction・固定formatを公開する。source consumerは利用アプリに開く。sink producerを自社hostの1つに限定する。client接続・切断とstream開始・停止をprovider queueへ直列化する。段階5のsource利用数を `setSourceActive:` へつなぐ。
2. `CMIOExtensionClient` から実際に取得できる識別情報を調べ、OSが検証した署名情報に基づいてhostを確認する。PID、表示名、未検証signing ID文字列だけで許可しない。許可後に `setAuthorizedProducer:` を呼び、切断・世代変更ではnilに戻す。不許可client、同時2 producer、host終了時の挙動を試す。
3. host側でCoreMediaIOからstable device IDとsink directionを列挙する。`CMIOStreamCopyBufferQueue`、stream開始・停止、queue変更通知、Extension再起動後の再列挙を実装する。publisher状態を unavailable / opening / ready / backpressure / disconnected / stopped に分け、UIへ正確に通知する。
4. hostの動く生成映像を1280×720・420vへ正規化し、format descriptionとCMSampleBufferを作って有限queueへ入れる。成功・失敗・queue callback・stop時のsample参照管理を表にしてコードレビューし、リークと二重releaseを試す。RTC callbackから将来呼んでも待機しないよう、decoded画像は最新優先で置き換える。
5. Extension側で `consumeSampleBufferFromClient:completionHandler:` の結果を `KMFrameRelay` へ渡す。1世代1件のoutstanding consume、sequence、discontinuity、`hasMore`、errorを扱う。古い世代のcallbackとproducer切断後の画像を破棄する。
6. 段階5の生成映像送出をrelayへ切り替え、同じ30fps timerから `tickAtHostTimeNs:` を呼ぶ。到着ごとの無制限なsendを避ける。表示したsequenceの時刻で `notifyScheduledOutputChanged:` を送る。source停止中もsink消費と古い画像の失効を続け、再開時に過去の画像を出さない。

## 試験手順

host生成映像のframe counterを、sourceから一般のカメラ利用アプリで撮る。投入→consume→source送出の各sequenceとhost時刻を照合する。sink queueを満杯にしてbackpressureと最新画像への置換を確認する。producer停止、強制終了、Extension再起動、source停止・再開、2以上のconsumer、別アプリのsink投入試行を個別に試す。切断後は待機画面へ移り、旧世代の画像が戻らないことを動画とログで確認する。

## 完了条件

署名で確認したhostだけがsinkへ投入でき、許可されないclientは映像を置換できない。hostの動く生成映像がsource consumerに届き、30fps・単調な時刻・sequence通知・backpressure・切断後の待機画面が確認できる。複数consumerの利用がproducerの接続状態を壊さない。sampleの所有権、queue容量、破棄条件、再接続時の世代更新をコードと実行結果の両方で記録する。

## 判断が必要な点

clientの署名確認とCoreMediaIO queueの参照管理には実機確認が要る。想定と異なればAPIの所有・通知順を記録し、方式を見直す。標準sinkの制約を測定する前に独自IPCへ切り替えない。[設計判断](../../docs/macos/08_DECISIONS_AND_REFERENCES.md)のD03とD04を参照する。

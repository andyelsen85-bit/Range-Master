CREATE TYPE "public"."bill_payment_status" AS ENUM('PAID');--> statement-breakpoint
CREATE TABLE "bill_payments" (
  "id" serial PRIMARY KEY NOT NULL,
  "external_id" text NOT NULL,
  "spieler_id" integer NOT NULL,
  "datum" date NOT NULL,
  "status" "bill_payment_status" DEFAULT 'PAID' NOT NULL,
  "terminal_id" text,
  "source" text DEFAULT 'TERMINAL' NOT NULL,
  "paid_at" timestamp with time zone DEFAULT now() NOT NULL,
  "marked_by_admin_id" integer,
  "marked_by_api_key_id" integer,
  CONSTRAINT "bill_payments_external_id_unique" UNIQUE("external_id")
);--> statement-breakpoint
ALTER TABLE "bill_payments" ADD CONSTRAINT "bill_payments_spieler_id_spieler_id_fk" FOREIGN KEY ("spieler_id") REFERENCES "public"."spieler"("id") ON DELETE restrict ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "bill_payments" ADD CONSTRAINT "bill_payments_marked_by_admin_id_spieler_id_fk" FOREIGN KEY ("marked_by_admin_id") REFERENCES "public"."spieler"("id") ON DELETE restrict ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "bill_payments" ADD CONSTRAINT "bill_payments_marked_by_api_key_id_api_keys_id_fk" FOREIGN KEY ("marked_by_api_key_id") REFERENCES "public"."api_keys"("id") ON DELETE restrict ON UPDATE no action;--> statement-breakpoint
CREATE INDEX "bill_payments_datum_idx" ON "bill_payments" USING btree ("datum");--> statement-breakpoint
CREATE UNIQUE INDEX "bill_payments_one_paid_per_player_day" ON "bill_payments" USING btree ("spieler_id","datum") WHERE "status" = 'PAID';--> statement-breakpoint
ALTER TABLE "spiele" ADD COLUMN "confirmed_launches" integer DEFAULT 0 NOT NULL;
--> statement-breakpoint
ALTER TABLE "sale_events" ADD COLUMN "product_name" text;--> statement-breakpoint
ALTER TABLE "sale_events" ADD COLUMN "product_category" "product_category";--> statement-breakpoint
UPDATE "sale_events" s SET "product_name" = p."name", "product_category" = p."category" FROM "products" p WHERE p."id" = s."product_id";--> statement-breakpoint
ALTER TABLE "sale_events" ALTER COLUMN "product_name" SET NOT NULL;--> statement-breakpoint
ALTER TABLE "sale_events" ALTER COLUMN "product_category" SET NOT NULL;